#include "StdAfxEd.h"
#include "VACompletionSet.h"
#include "VAAutomation.h"
#include "EdDll.h"
#include "FILE.H"
#include "ParseThrd.h"
#include "PROJECT.H"
#include "DevShellAttributes.h"
#include "FileTypes.h"
#include "..\common\ConsoleWnd.h"
#include "VAParse.h"
#include "VAClassView.h"
#include "rbuffer.h"
#include "IdeSettings.h"
#include "AutotextManager.h"
#include "FindReferences.h"
#include "VARefactor.h"
#include "ScreenAttributes.h"
#include "wt_stdlib.h"
#include "Addin\MiniHelpFrm.h"
#include "..\common\TempAssign.h"
#include "SubClassWnd.h"
#include "SyntaxColoring.h"
#include "VaService.h"
#include "VaTimers.h"
#include "MenuXP\MenuXP.h"
#include "WtException.h"
#include "DevShellService.h"
#include "Guesses.h"
#include "ToolTipEditCombo.h"
#include "SymbolRemover.h"
#include "focus.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "LiveOutlineFrame.h"
#include "Expansion.h"
#include "VaOptions.h"
#include "ProjectInfo.h"
#include <map>
#include <list>
#include <set>
#include "FileId.h"
#include <regex>
#include "Directories.h"
#include "LogVersionInfo.h"
#include <tlhelp32.h>
#include "RegKeys.h"
#include "Registry.h"
#include "VaAddinClient.h"
#include <psapi.h>
#include "VAAutomation17.h"
#pragma comment(lib, "Psapi.lib")

#if 0
// alt+g works on these
#include "..\tests\VaAutomationTests\MfcApplication\AstReadme.txt"
#include "..\tests\VaAutomationTests\ast.txt"

// with source links file plugin, double-click works on these
// ..\tests\VaAutomationTests\MfcApplication\AstReadme.txt
// ..\tests\VaAutomationTests\ast.txt
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static const TCHAR kRunAllTestsPrefix[] = _T("RunAllTests");
static const TCHAR kRunAllTestsInSolution[] = _T("RunAllTestsInSolution");
static const TCHAR kRunAllTestsInProject[] = _T("RunAllTestsInProject");
static const TCHAR kRunAllTestsInFile[] = _T("RunAllTestsInFile");
static const TCHAR RunAllTestsInOpenFiles[] = _T("RunAllTestsInOpenFiles");
static const TCHAR kRunAllTestsToStress[] = _T("RunAllTestsToStress");

static const TCHAR kStartOfTest[] = _T("test: ");
static const TCHAR kEOTCharsInSrc[] = _T(": \t\r\n");
static const TCHAR kStatusFail[] = _T(" **FAILED**\r\n");
static const TCHAR kStatusPass[] = _T(" .\r\n");
static const TCHAR kStatusSkippedRepeat[] = _T(" * skipped repeat\r\n");

static const size_t kStartOfTestLen = sizeof(kStartOfTest) / sizeof(TCHAR) - 1;
static const size_t kStatusFailLen = sizeof(kStatusFail) / sizeof(TCHAR) - 1;
static const size_t kStatusPassLen = sizeof(kStatusPass) / sizeof(TCHAR) - 1;
static const size_t kStatusSkippedRepeatLen = sizeof(kStatusSkippedRepeat) / sizeof(TCHAR) - 1;
#if defined(AVR_STUDIO)
static const char* kShellNameUsedInCloseAll = "AtmelStudio";
#else
static const char* kShellNameUsedInCloseAll = "Microsoft Visual Studio";
#endif

bool gTestsActive = false;
std::unique_ptr<ConsoleWnd> sConsole;
VAAutomationLogging* gTestLogger = NULL;
CStringW gTestSnippetSubDir;
CStringA gTestVaOptionsPage;
static CComQIPtr<EnvDTE::OutputWindowPane> s_IdeOutputWindow;
const bool kLimitAutoRunLog = false;
static int sExecLine = 0; // to debug exceptions
static bool gStressTest = false;
std::unique_ptr<VAAutomationReRun> gReRun;
const int kMaxFileOpenCount = 5;
WTString gDirectiveInvokedFrom;
extern int gExternalMenuCount;

bool HandleSkipTagInTestCode(WTString& m_testCode);

class TestOverrunException : public WtException
{
  public:
	TestOverrunException(LPCTSTR msg)
	    : WtException(msg)
	{
	}
	~TestOverrunException() = default;
};

CStringW VAAutomationReRun::GetUserSettingStr(LPCWSTR name, const CStringW& default_value)
{
	if (!name)
		return default_value;

	CStringW name_str(name);
	name_str.Trim();

	_ASSERTE(!name_str.IsEmpty());

	if (name_str.IsEmpty())
		return default_value;

	name_str.MakeUpper();

	CStringW resToken;
	int curPos = 0;

	resToken = mUserSettings.Tokenize(L";", curPos);
	while (!resToken.IsEmpty())
	{
		int eqPos = resToken.Find(L"=");
		if (eqPos >= 0)
		{
			CStringW resName = resToken.Mid(0, eqPos);
			resName.Trim();
			resName.MakeUpper();

			if (resName == name_str)
				return resToken.Mid(eqPos + 1);
		}

		resToken = mUserSettings.Tokenize(L";", curPos);
	};

	return default_value;
}

bool VAAutomationReRun::GetUserSettingBool(LPCWSTR name, bool default_value)
{
	CStringW val = GetUserSettingStr(name, CStringW());
	val.Trim();

	if (!val.IsEmpty())
	{
		if (val == L"0")
			return false;

		if (val == L"1")
			return true;

		int num = StrToIntW(val);

		if (num)
			return true;

		val.MakeUpper();

		if (val == "NO" || val == "OFF" || val == "FALSE")
			return false;

		if (val == "YES" || val == "ON" || val == "TRUE")
			return true;

		return default_value;
	}

	return default_value;
}

bool VAAutomationReRun::Init(bool isAutomatedRun)
{
	LPCSTR keys[] = { ID_RK_APP,
#if !defined(RAD_STUDIO)
		              ID_RK_APP_KEY,
#endif
		              0 };

	mUserSettings.Empty();
	for (LPCSTR* keyStrP = keys; *keyStrP; keyStrP++)
	{
		HKEY hKey = nullptr;
		LSTATUS err = RegOpenKeyEx(HKEY_CURRENT_USER, *keyStrP, 0, KEY_QUERY_VALUE, &hKey);

		if (ERROR_SUCCESS == err)
		{
			WCHAR szBuffer[1024];
			DWORD dwBufferSize = sizeof(szBuffer);
			if (ERROR_SUCCESS == RegQueryValueExW(hKey, L"AstRerunSettings", 0, NULL, (LPBYTE)szBuffer, &dwBufferSize))
			{
				mUserSettings = szBuffer;
				RegCloseKey(hKey);
				break;
			}
		}

		RegCloseKey(hKey);
	}

	return InitFromUserSettings(isAutomatedRun);
}

bool VAAutomationReRun::InitFromUserSettings(bool isAutomatedRun)
{
	mAllowed = GetUserSettingBool(L"RERUN", false);
	mPrompts = !isAutomatedRun && GetUserSettingBool(L"PROMPT", false);
	mRelog = GetUserSettingBool(L"RELOG", true);
	mOnlyListed = GetUserSettingBool(L"ONLY_LISTED",
	                                 GetUserSettingBool(L"ONLYLISTED", GetUserSettingBool(L"ONLY LISTED", false)));
	mAutomat = isAutomatedRun || GetUserSettingBool(L"AUTO", false);
	mClipboard = GetUserSettingBool(L"CLIPBOARD", false);

	mSrcLog.Empty();

	// path used to debug failed logs
	mPath = GetUserSettingStr(L"PATH", CStringW());
	if (mPath.Find('%') >= 0)
	{
		DWORD kBufLen = ExpandEnvironmentStringsW(mPath, nullptr, 0);
		const std::unique_ptr<WCHAR[]> buffPtr(new WCHAR[kBufLen]);
		LPWSTR buff = &buffPtr[0];
		*buff = 0;
		ExpandEnvironmentStringsW(mPath, buff, kBufLen);
		mPath = buff;
	}

	return mAllowed;
}

bool VAAutomationReRun::PrepareTestsToReRun(LPCTSTR batchTest, bool isAutomatedRun)
{
	if (!batchTest)
		return false;

	if (gStressTest)
		return false;

	if (StrStr(batchTest, kRunAllTestsPrefix) != batchTest)
		return false;

	if (StrStr(batchTest, kRunAllTestsInFile) == batchTest)
		return false;

	if (
#ifdef VA_CPPUNIT
	    !mAllowed &&
#endif // _DEBUG
	    !Init(isAutomatedRun))
	{
		return false;
	}

#ifndef VA_CPPUNIT
	if (!GlobalProject)
		return false;

	CStringW slnFile = GlobalProject->SolutionFile();

	if (slnFile.GetLength())
#endif
	{
		CStringW testLogFile;
		bool is_user_log = false; // if user already approved file

		if (mClipboard && mPrompts)
		{
			WTString clipBoardText = ::GetClipboardText(HWND_DESKTOP);

			if (!clipBoardText.IsEmpty() && clipBoardText.Find(kStartOfTest) >= 0)
			{
				WTString msg(_T("Clipboard contains tests available to re-run.\r\n"));

				if (mOnlyListed)
					msg.append(_T("\r\nDo you want to re-run failed tests from Clipboard?"));
				else
					msg.append(_T("\r\nDo you want to re-run failed and not yet launched tests from Clipboard?"));

				if (IDYES == WtMessageBox(msg, _T("Re-run from Clipboard..."), MB_YESNO))
				{
					mSrcLog = clipBoardText;
					is_user_log = true;
				}
			}
		}

		if (mSrcLog.IsEmpty())
		{
			if (!mPath.IsEmpty())
			{
				if (IsDir(mPath))
				{
					CFileDialog dlg(TRUE);

					dlg.m_ofn.lpstrTitle = "Select file for AST re-run";
					dlg.m_ofn.lpstrFilter = "AST log files (*.log;*.tmp)|"
					                        "*.log;*.tmp|"
					                        "Text files (*.txt)|"
					                        "*.txt|"
					                        "All files (*.*)|"
					                        "*.*|"
					                        "|";

					CString dir(mPath); // dir keeps pointer alive!
					dlg.m_ofn.lpstrInitialDir = dir;

					if (dlg.DoModal() == IDOK)
					{
						WTString filename((LPCTSTR)dlg.GetPathName()); // return full path and filename
						testLogFile = filename.Wide();
						is_user_log = true;
					}
					else
					{
						if (mPrompts)
							WtMessageBox("Rerun canceled; starting standard run.", "AST", MB_OK);
						return false; // user does not want rerun
					}
				}
				else if (IsFile(mPath))
				{
					testLogFile = mPath;
					is_user_log = false; // still ask if prompt is required
				}
				else
				{
					if (mPrompts)
						WtMessageBox("Path specified is invalid; starting standard run.", "AST", MB_OK);
					return false;
				}
			}

#ifndef VA_CPPUNIT
			if (testLogFile.IsEmpty() || !IsFile(testLogFile))
			{
				testLogFile = Path(slnFile) + L'\\' + GetBaseNameNoExt(slnFile) + L".tmp";
				is_user_log = false;
			}
#endif

			if (IsFile(testLogFile))
				mSrcLog = ReadFile(testLogFile);
		}

		if (!mSrcLog.IsEmpty())
		{
			if (!is_user_log)
			{
				// check for TMP and current run compatibility
				// User defined files are not checked for compatibility
				WTString hdr(_T("Batch run type: "));
				hdr += batchTest;

				if (mSrcLog.Find(hdr) == -1)
				{
					if (mPrompts)
						WtMessageBox("Rerun is not compatible; starting standard run.", "AST", MB_OK);
					return false; // not compatible
				}
			}

			if (mPrompts && !is_user_log)
			{
				WTString msg(_T("The file appropriate for AST re-run exists:\r\n"));
				msg.append((WTString("$SolutionDir$\\") + WTString(GetBaseName(testLogFile))).c_str());
				msg.append(_T("\r\n\r\nDo you want to continue in re-run?"));

				if (IDNO == WtMessageBox(msg, _T("Re-run available..."), MB_YESNO))
				{
					mSrcLog.Empty();
					return false;
				}
			}

			for (LPCTSTR search_begin = StrStr(mSrcLog.c_str(), kStartOfTest); search_begin && *search_begin;
			     search_begin = StrStr(search_begin, kStartOfTest))
			{
				// allow only instances after a white space
				// unexpected behavior could happen in case of messed log file
				if (search_begin > mSrcLog && !wt_isspace(search_begin[-1]))
				{
					// move away to not catch the same instance in next search
					search_begin += kStartOfTestLen;
					continue;
				}

				search_begin += kStartOfTestLen;

				LPCTSTR test_name_end = _tcspbrk(search_begin, _T(" \r\n\t"));
				if (test_name_end == nullptr)
					test_name_end = search_begin + _tcslen(search_begin);

				WTString full_test(search_begin, ptr_sub__int(test_name_end, search_begin));

				if (!full_test.IsEmpty())
				{
					// find file extension delimiter '.'
					int file_name_end = full_test.ReverseFind(_T("."));

					if (file_name_end == -1)
					{
						// should never happen, (but could if source file has no extension)
						// then it would be hard to determine what is file and what is a test name
						// example: source_file_name_test_name ?? which '_' is delimiter?
						_ASSERTE(!"Failed to split test name!");
						continue;
					}
					else
					{
						file_name_end = full_test.Find(_T("_"), file_name_end);
						if (file_name_end == -1)
						{
							_ASSERTE(!"Failed to split test name!");
							continue;
						}
					}

					WTString file = full_test.Mid(0, file_name_end);
					WTString test = full_test.Mid(file_name_end + 1);

					search_begin = test_name_end;

					enum class TestStatus
					{
						FAIL,
						SKIP,
						PASS
					};
					TestStatus test_status = TestStatus::FAIL;

					if (search_begin && *search_begin)
					{
						LPCTSTR line_end = _tcspbrk(search_begin, _T("\r\n"));
						if (line_end == nullptr)
							line_end = search_begin + _tcslen(search_begin);

						LPCTSTR status = StrStr(search_begin, kStatusPass);
						if (status && status < line_end)
						{
							// " ." means PASS
							test_status = TestStatus::PASS;
							search_begin = status + kStatusPassLen;
						}
						else
						{
							status = StrStr(search_begin, kStatusSkippedRepeat);
							if (status && status < line_end)
							{
								// "* skipped repeat" means SKIP
								test_status = TestStatus::SKIP;
								search_begin = status + kStatusSkippedRepeatLen;
							}
							else
							{
								status = StrStr(search_begin, kStatusFail);
								if (status) // no EOL check because **FAILED** may be on one of following lines
								{
									// "**FAILED**"
									test_status = TestStatus::FAIL;
									search_begin = status + kStatusFailLen;
								}
								else
								{
									// _ASSERTE(!"Test without status?");
								}
							}
						}
					}

#if defined(_DEBUG) || defined(VA_CPPUNIT)
					mAllInLogDbg[file].insert(test);
#endif // _DEBUG

					if (test_status == TestStatus::SKIP)
					{
						// Skip - do nothing
					}
					else if (test_status == TestStatus::PASS)
					{
						if (mOnlyListed)
						{
							auto it_set = mFailedTests.find(file);
							if (it_set != mFailedTests.end())
							{
								auto& test_set = it_set->second;
								test_set.erase(test);
								if (test_set.empty())
									mFailedTests.erase(it_set);
							}
						}
						else
						{
							mPassedTests[file].insert(test);
						}
					}
					else if (test_status == TestStatus::FAIL && mOnlyListed)
					{
						mFailedTests[file].insert(test);
					}
				}
			}
		}
	}

	if (!mFailedTests.empty() || !mPassedTests.empty())
		return true;

	if (mAllowed && mPrompts)
		WtMessageBox("Rerun not available; starting standard run.", "AST", MB_OK);

	mSrcLog.Empty();
	return false;
}

size_t VAAutomationReRun::FindTestsInSrc(LPCTSTR in_str, bool exclude_batch_tests, bool apply_skip,
                                         std::function<void(LPCTSTR, LPCTSTR)> func)
{
	_ASSERTE(in_str && *in_str);

	if (!in_str || !(*in_str))
		return 0;

	size_t rslt = 0;

	const WTString autotextStart(_T(VA_AUTOTES_STR));
	const int kRunAllTestsPrefixLen = strlen_i(kRunAllTestsPrefix);

	const auto skip_test = [](LPCTSTR buf) -> bool {
		LPCTSTR code = StrStrI(buf, _T("<CODE"));
		if (code)
		{
			LPCTSTR endCode = StrStrI(code, _T("</CODE>"));
			LPCTSTR startCode = StrStr(code, _T(">"));
			if (startCode && endCode)
			{
				code = startCode + 1;

				if (endCode <= code)
					return true; // skip invalid (empty) tests

				WTString testCode(code, ptr_sub__int(endCode, code));

				if (StrStrI(testCode.c_str(), _T("<code"))) // Sanity check
				{
					_ASSERTE(!"Invalid test case - contains <code> tag.");
					return false; // pass - let AST engine find the problem
				}

				// TODO: could be more efficient w/o WTString
				return HandleSkipTagInTestCode(testCode);
			}
		}

		return false;
	};

	LPCTSTR test_end = nullptr;

	for (LPCTSTR test_start = StrStr(in_str, autotextStart.c_str()); test_start && *test_start;
	     test_start = StrStr(test_start, autotextStart.c_str()))
	{
		// move index behind the "VAAutotext:" string
		test_start += autotextStart.GetLength();

		// find end of test name
		test_end = _tcspbrk(test_start, kEOTCharsInSrc);
		if (!test_end)
			test_end = test_start + _tcslen(test_start);

		_ASSERTE(test_end > test_start);

		if (test_end > test_start)
		{
			// exclude batch tests
			if (!exclude_batch_tests || StrNCmp(test_start, kRunAllTestsPrefix, kRunAllTestsPrefixLen) != 0)
			{
				// exclude tests having appropriate <skip> tag
				if (!apply_skip || !skip_test(test_end))
				{
					// pass test name range to functor
					func(test_start, test_end);
					rslt++;
				}
			}

			test_start = test_end;
		}
	}

	return rslt;
}

void RetryRenameFile(const CStringW& from, const CStringW& to)
{
	for (int idx = 0; idx < 5; ++idx)
	{
		if (!_wrename(from, to))
			break;

		Sleep(1000);

		if (4 == idx)
		{
			// can't log at this point -- logs are closed and have been moved around.
			// try to block VS exit by displaying msgbox...
			WtMessageBox("Log file rename failed multiple times.", "VA Automation", MB_OK | MB_ICONERROR);
		}
	}
}

static BOOL g_AutomatedRun = FALSE;

class AutomationLog
{
	CFileW m_AutoRunLogFile;

  public:
	~AutomationLog()
	{
		if (m_AutoRunLogFile.m_hFile && (HANDLE)m_AutoRunLogFile.m_hFile != INVALID_HANDLE_VALUE)
			CloseLog();
	}

	void CloseLog()
	{
		const CStringW tlogFile{m_AutoRunLogFile.GetFilePath()};
		const CStringW logFile = Path(tlogFile) + L"\\" + GetBaseNameNoExt(tlogFile) + L".log";
		const CStringW bakLogFile = Path(tlogFile) + L"\\" + GetBaseNameNoExt(tlogFile) + L".bak";

		m_AutoRunLogFile.Close();
		if (::IsFile(bakLogFile))
			::DeleteFileW(bakLogFile);

		if (::IsFile(logFile))
			::RetryRenameFile(logFile, bakLogFile);

		::RetryRenameFile(tlogFile, logFile); // Rename to signal test is finished.
	}

	void LogText(const WTString& msg)
	{
		if (!m_AutoRunLogFile.m_hFile || (HANDLE)m_AutoRunLogFile.m_hFile == INVALID_HANDLE_VALUE)
		{
			// Open log file
			CStringW slnFile = GlobalProject->SolutionFile();
			if (slnFile.GetLength())
			{
				CStringW testLogFile = Path(slnFile) + L'\\' + GetBaseNameNoExt(slnFile) + L".tmp";
				m_AutoRunLogFile.Open(testLogFile, (g_AutomatedRun ? CFile::modeNoTruncate : 0u) | CFile::modeCreate |
				                                       CFile::shareDenyWrite | CFile::modeWrite | CFile::modeNoInherit);
			}
		}

		if (m_AutoRunLogFile.m_hFile && (HANDLE)m_AutoRunLogFile.m_hFile != INVALID_HANDLE_VALUE)
		{
			m_AutoRunLogFile.Write(msg.c_str(), (UINT)msg.GetLength());
			m_AutoRunLogFile.Flush();
		}
	}
};

std::unique_ptr<AutomationLog> s_AutomationLog;

void ReleaseAstResources()
{
	s_AutomationLog = nullptr;
	_ASSERTE(!gTestLogger);
	gTestLogger = nullptr;
	s_IdeOutputWindow = nullptr;
	gReRun = nullptr;
	sConsole = nullptr;
}

WTString GetStringArgValue(const WTString& tagstr)
{
	int pos = tagstr.Find(':');
	if (-1 != pos)
		return tagstr.Mid(pos + 1);
	return NULLSTR;
}

std::vector<WTString> GetStringArgValues(const WTString& tagstr, TCHAR argsDelim, bool includeStart = false)
{
	std::vector<WTString> vec;

	int spos = 0;
	int epos = tagstr.Find(':');
	TCHAR delimStr[] = {argsDelim, '\0'};

	while (epos > spos)
	{
		if (spos == 0)
		{
			if (includeStart)
				vec.push_back(tagstr.Mid(spos, epos - spos));
		}
		else
		{
			vec.push_back(tagstr.Mid(spos, epos - spos));
		}

		spos = epos + 1;
		epos = tagstr.Find(delimStr, spos);
	}

	if (spos < tagstr.length())
		vec.push_back(tagstr.Mid(spos));

	return vec;
}

int GetArgValue(const WTString& tagstr)
{
	WTString val = GetStringArgValue(tagstr);
	int pos = val.Find("0x");
	if (0 == pos)
		return atox(val.c_str() + 2); // foo:0x10

	if (!val.IsEmpty())
	{
		pos = val.Find("h");
		if (-1 != pos && pos == val.GetLength() - 1)
			return atox(val.c_str()); // foo:10h

		return atoi(val.c_str()); // foo:16
	}
	return 0;
}

bool GetBoolArgValue(const WTString& tagstr)
{
	const int res = GetArgValue(tagstr);
	return res != 0;
}

void WINAPI ShowVaParamInfo()
{
	if (g_currentEdCnt)
		g_currentEdCnt->DisplayToolTipArgs(true);
}

LPCWSTR WINAPI GetActiveDocument()
{
	static CStringW sDocName;
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	CComPtr<EnvDTE::Document> pDocument;
	gDte->get_ActiveDocument(&pDocument);
	if (pDocument)
	{
		CComBSTR bstrName;
		pDocument->get_FullName(&bstrName);
		sDocName = bstrName;
	}
	else
		sDocName.Empty();

	return sDocName;
}

LPCWSTR
GetActiveDocumentSafe()
{
	// give dte a chance to sync after nav to next test
	Sleep(250);
	return RunFromMainThread<LPCWSTR>(GetActiveDocument);
}

void NotifyTestsActive(bool active)
{
	static HANDLE mtx = nullptr;

	if (active && !s_AutomationLog)
		s_AutomationLog = std::make_unique<AutomationLog>();

	if (!active)
		gTestVaOptionsPage.Empty();

	if (active && mtx == nullptr)
	{
		// #VA_AST_MUTEX
		char buff[MAX_PATH];
		if (0 < sprintf_s(buff, MAX_PATH, "VA_AST_MUTEX_%ld", ::GetCurrentProcessId()))
			mtx = ::CreateMutexA(nullptr, TRUE, buff);
	}
	else if (!active && mtx != nullptr)
	{
		::CloseHandle(mtx);
		mtx = nullptr;
	}

	gTestsActive = active;
	RunFromMainThread([active]() // must be synced #VaInteropService_Synced
	                  {
		                  if (gVaInteropService)
			                  gVaInteropService->SetAstEnabled(active);
	                  });
}

void ConsoleTraceDirect(const WTString& wtmsg)
{
	CStringW msgW(wtmsg.Wide());
	if (s_IdeOutputWindow)
		s_IdeOutputWindow->OutputString(CComBSTR(msgW));
	if (s_AutomationLog)
		s_AutomationLog->LogText(wtmsg); // Log all console output to logfile
	OutputDebugStringW(msgW);

	// wprintf doesn't work with CStringW in the console that we created
	printf("%s", wtmsg.c_str());
}

void ConsoleTraceDirect(LPCTSTR m)
{
	WTString msg(m);
	ConsoleTraceDirect(msg);
}

// void
// ConsoleTrace(const WTString& msg)
//{
//	ConsoleTraceDirect(msg);
//}

#define ConsoleTrace(format, ...)                        \
	if (false)                                           \
		_snprintf_s(nullptr, 0, 0, format, __VA_ARGS__); \
	else                                                 \
		__ConsoleTrace(format, __VA_ARGS__)

void __ConsoleTrace(LPCSTR fmt, ...)
{
	const int kBuflen = 1024;
	const std::unique_ptr<char[]> msgVec(new char[kBuflen]);
	char* msg = &msgVec[0];

	va_list args;
	va_start(args, fmt);
	vsnprintf_s(msg, kBuflen, _TRUNCATE, fmt, args);
	va_end(args);

	ConsoleTraceDirect(msg);
}

void WaitUiThreadEvents()
{
	_ASSERTE(g_mainThread != GetCurrentThreadId());
	// allow pending sendEvents to be queued
	Sleep(150);
	// loop here, without dispatching on ui thread
	BOOL msgInQueue = FALSE;
	const DWORD kStart(::GetTickCount());
	while (SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_THREAD_AUTOMATION_PEEK, (WPARAM)&msgInQueue, 0))
	{
		if (!msgInQueue)
		{
			if (gProcessingMessagesOnUiThread)
			{
				// if we are processing messages on ui thread, keep waiting
				ConsoleTrace(" (wuit:c1) ");
			}
#if !defined(VA_CPPUNIT)
			else if (GetPendingUiThreadTaskCount())
			{
				// if we have any pending thread tasks, keep waiting
				ConsoleTrace(" (wuit:c2) ");
			}
#endif
			else
				break;
		}

		if ((::GetTickCount() - kStart) > (1 * 60000))
		{
			WTString msg("\r\nERROR: WaitUiThreadEvents timeout\r\n");
			ConsoleTraceDirect(msg);
			break;
		}

		Sleep(100);

		if (gTestLogger && !gTestLogger->IsOK())
			return;

		if ((::GetTickCount() - kStart) > 500)
		{
			if (gProcessingMessagesOnUiThread)
			{
				// if we are processing messages on ui thread, keep waiting
				ConsoleTrace(" (wuit:c3) ");
			}
#if !defined(VA_CPPUNIT)
			else if (!GetPendingUiThreadTaskCount())
			{
				// if we don't have any pending thread tasks, just timeout without error
				ConsoleTrace(" (wuit:b1) ");
				break;
			}
#endif
		}
	}
}

void WaitThreads(BOOL waitForRefactoring /*= TRUE*/)
{
	const DWORD startTicks = GetTickCount();
	EdCntPtr prevEdCnt = g_currentEdCnt;
	WaitUiThreadEvents();
	if (Psettings->m_enableVA)
	{
		int menuCnt = 0;
		const bool wasMenuActive = PopupMenuXP::IsMenuActive() || gExternalMenuCount;
		// wait:threads in ast script code is used for much more than waiting for
		// any threads.  It is being used to wait for any outstanding work
		// that could be relevant to the current test.
		// Right now, it checks for active threads and valid scope.
		// Probably should also check to see if g_currentEdCnt->m_FileHasTimerForFullReparse?
		// Maybe also should see if g_currentEdCnt has ID_GETBUFFER timer?
		while (g_threadCount || (waitForRefactoring && RefactoringActive::IsActive()) ||
		       ((g_ParserThread->HasQueuedItems() || g_ParserThread->IsNormalJobActiveOrPending()) &&
		        !RefactoringActive::IsActive()))
		{
			Sleep(20);
			const DWORD curTick = GetTickCount();

			if (waitForRefactoring && RefactoringActive::IsActive() && wasMenuActive &&
			    (PopupMenuXP::IsMenuActive() || gExternalMenuCount) && curTick > startTicks + 7000)
			{
				// orphaned menu?
				if (gTestLogger)
				{
					if (PopupMenuXP::IsMenuActive())
					{
						ConsoleTrace("AST delay: Orphaned menu (A) %d\r\n", menuCnt);
						PopupMenuXP::CancelActiveMenu();
					}

					if (gExternalMenuCount)
					{
						ConsoleTrace("AST delay: Orphaned external menu (A) %d\r\n", menuCnt);
						gTestLogger->Typomatic::TypeString("<esc>", FALSE);
					}

					if (PopupMenuXP::ActiveMenuCount() || gExternalMenuCount)
					{
						gTestLogger->Typomatic::TypeString("<esc>", FALSE);
						if (PopupMenuXP::ActiveMenuCount())
							ConsoleTrace("AST delay: Orphaned menu (B) %d\r\n", menuCnt);
						else if (gExternalMenuCount)
							ConsoleTrace("AST delay: Orphaned external menu (B) %d\r\n", menuCnt);
						else
							ConsoleTrace("AST delay: Orphaned menu recovered by <esc> (B) %d\r\n", menuCnt);
					}

					gTestLogger->Typomatic::TypeString("<esc>", FALSE);
					WaitUiThreadEvents();
					++menuCnt;
				}
				break;
			}

			const int kMinutes = 2;
			if (curTick > startTicks + (kMinutes * 60000))
			{
				// bail
				if (gTestLogger)
				{
					gTestLogger->Typomatic::TypeString("<esc>", FALSE);
					Sleep(50);
					WaitUiThreadEvents();

					WTString msg;
					msg.WTFormat("ERROR: WAIT:THREADS %d minutes elapsed, quitting current test", kMinutes);
					throw WtException(msg);
				}
				break;
			}

			if (gTestLogger && !gTestLogger->IsOK())
				return;
		}

		EdCntPtr ed = g_currentEdCnt;
		for (int i = 0; i < 200 && ed && (!ed->m_isValidScope && CAN_USE_NEW_SCOPE(gTypingDevLang)); i++)
		{
			Sleep(20);

			ed = g_currentEdCnt;
			if (0 == i && ed)
			{
				BOOL hasSel;
				RunFromMainThread([&ed, &hasSel]() { hasSel = ed->HasSelection(); });
				if (hasSel)
					break;
			}

			if (i && gTestLogger && !gTestLogger->IsOK())
				return;
		}
	}

	EdCntPtr newEd = g_currentEdCnt;
	if (newEd && newEd != prevEdCnt)
	{
		// [case: 53390] colin doesn't like to use <wait:time> when doing alt+g between
		// different files; this should also help with alt+o
		newEd->KillTimer(ID_TIMER_GETSCOPE);
		newEd->SetTimer(ID_TIMER_GETSCOPE, 150, NULL);
		newEd->m_isValidScope = FALSE;
		Sleep(600);
		WaitThreads(FALSE);
	}

	vCatLog("Editor.Events", "VaEventAT   WaitThreads time'%ld'", GetTickCount() - startTicks);
}

// caller must ensure proper lifetime of cmd (string literals are safe)
void RunVSCommand(LPCSTR cmd, BOOL invalidateBuf = TRUE, BOOL waitThrds = TRUE, BOOL postIfNotWait = FALSE)
{
	// do not use statics in here due to re-entrancy while VS dialogs are running
	vCatLog("Editor.Events", "VaEventAT  RunVSCommand '%s'", cmd);
	try
	{
		if (waitThrds)
		{
			// change 15961
			// Changed to PostMessage then WaitThreads for VA to process it,  for VA's context menus.
			// If a context menu is displayed, it's message loop will process the WaitThreads(WM_VA_THREAD_AUTOMATION)
			// allowing script to continue. We could create <POSTCMD:VAssistX.VaSnippetInsert> if there is a problem,
			// but this seems safe and less confusing for the script writer.

			EdCntPtr ed = g_currentEdCnt;
			if (ed && ed->m_isValidScope)
			{
				// Invalidate Scope so we wait until VA process the command.
				ed->KillTimer(ID_TIMER_GETSCOPE);
				ed->SetTimer(ID_TIMER_GETSCOPE, 150, NULL);
				ed->m_isValidScope = FALSE;
			}

			PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)cmd, 0);
			WaitThreads(FALSE);
		}
		else if (postIfNotWait)
		{
			PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)cmd, 0);
			WaitUiThreadEvents();
		}
		else
			SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)cmd, 0);

		// VA doesn't see any VS commands that modify the text, we need to ensure m_buf is up to date...
		// TODO: Should this logic be moved into VAM_EXECUTECOMMAND?
		if (invalidateBuf)
		{
			EdCntPtr ed(g_currentEdCnt);
			if (ed)
				ed->SendMessage(WM_COMMAND, VAM_GETBUF);
		}
	}
	catch (const WtException& e)
	{
		VALOGEXCEPTION("AST:");
		WTString msg;
		msg.WTFormat("ERROR: exception caught in VAAutomationThread RunVSCommand. WtEx %s\r\n", e.GetDesc().c_str());
		Log(msg.c_str());
		ConsoleTraceDirect(msg);
		::Sleep(1000);
	}
	catch (...)
	{
		VALOGEXCEPTION("AST:");
		const char* const msg = "ERROR: exception caught in VAAutomationThread RunVSCommand\r\n";
		Log(msg);
		ConsoleTraceDirect(msg);
		::Sleep(1000);
	}
}

void RunVSCommand(const WTString& cmd, BOOL invalidateBuf = TRUE, BOOL waitThrds = TRUE, BOOL postIfNotWait = FALSE)
{
	// this is not particularly safe, but the WTString version of RunVSCommand shouldn't cause re-entrancy...
	// see note re: statics in the other version of RunVSCommand.
	static WTString sCmd;
	sCmd = cmd;
	RunVSCommand(sCmd.c_str(), invalidateBuf, waitThrds, postIfNotWait);
}

void AstSyncVa()
{
	// <InvalidateBuf><CMD:VAssistX.ReparseCurrentFile><Wait:Threads>
	WaitThreads();
	if (g_currentEdCnt)
		g_currentEdCnt->SendMessage(WM_COMMAND, VAM_GETBUF);
	ClassViewSym.Empty();
	RunVSCommand("VAssistX.ReparseCurrentFile", FALSE);
	WaitThreads();
}

WTString TestNameOnCurrentLine()
{
	EdCntPtr ed = g_currentEdCnt;
	if (!ed)
	{
		// in debug builds, this was an assert, and change of focus or delay fixed
		// the problem.  in release builds, the ast thread would exception out.
		RunVSCommand("Window.ActivateDocumentWindow");
		Sleep(1500);
		ed = g_currentEdCnt;
		if (!ed)
		{
			// assume all tests in solution if not currently attached to a file
			if (gStressTest)
				return kRunAllTestsToStress;

			return kRunAllTestsInSolution;
		}
	}

	WTString buf;
	RunFromMainThread([&ed, &buf]() { buf = ed->GetBuf(); });
	if (buf.IsEmpty())
	{
		WaitThreads();
		RunFromMainThread([&ed, &buf]() {
			ed->SendMessage(WM_COMMAND, VAM_GETBUF);
			buf = ed->GetBuf();
		});
	}

	long bIdx;
	RunFromMainThread([&ed, &bIdx]() {
		long lp = (long)ed->LinePos();
		bIdx = ed->GetBufIndex(lp);
	});
	const long testPos = buf.Find(VA_AUTOTES_STR, bIdx);
	return TokenGetField(buf.c_str() + testPos + strlen(VA_AUTOTES_STR), ": \t\r\n");
}

CComQIPtr<EnvDTE::OutputWindowPane> GetIdeOutputWindow(CStringW toolWindowName)
{
	CComQIPtr<EnvDTE80::ToolWindows> ToolWindows;
	CComQIPtr<EnvDTE::OutputWindow> OutputWindow;
	CComQIPtr<EnvDTE::OutputWindowPanes> OutputWindowPanes;
	CComQIPtr<EnvDTE::OutputWindowPane> OutputWindowPane;

	// made this static because don't want to get into a situation where
	// the OutputWindowPanes->Add call is done multiple times in a single
	// session.
	static const bool bUseAstOutputPane = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, "UseAstOutputPane", true);
	if (!bUseAstOutputPane)
		return OutputWindowPane;

	if (gDte2)
		gDte2->get_ToolWindows(&ToolWindows);
	if (ToolWindows)
		ToolWindows->get_OutputWindow(&OutputWindow);
	if (OutputWindow)
		OutputWindow->get_OutputWindowPanes(&OutputWindowPanes);
	if (OutputWindowPanes)
		OutputWindowPanes->Item(CComVariant(toolWindowName), &OutputWindowPane);
	if (OutputWindowPanes && !OutputWindowPane)
		OutputWindowPanes->Add(CComBSTR(toolWindowName), &OutputWindowPane);
	return OutputWindowPane;
}

int WINAPI MessageBoxCB(LPCSTR msg, UINT msgType)
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	return WtMessageBox(msg, "VA Automation", msgType);
}

int RunMessageBox(LPCSTR msg, UINT msgType)
{
	return RunFromMainThread<int, LPCSTR, UINT>(MessageBoxCB, msg, msgType);
}

class EdCntChangedException : public WtException
{
  public:
	EdCntChangedException()
	    : WtException("EdCnt changed after SendMessage")
	{
	}
	~EdCntChangedException()
	{
	}
};

void VAAutomationTestInfo::PokeFindResults()
{
	CheckExceptionDlg();
	// Press escape to clear any remaining dialog, using Typomatic:: to preserve logging.
	Typomatic::TypeString("<esc>");
	Typomatic::TypeString("<esc>");

	if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
		CIdeFind::ShowFindResults1();
	else
		RunVSCommand(
		    "View.FindResults1"); // [case: 142365] this command is broken in VS2019 16.6+ (due to new Find UI?)

	Sleep(500);
	RunVSCommand("Edit.GoToFindResults1Location");
	Sleep(500);
	RunVSCommand("Window.ActivateDocumentWindow");
	Sleep(1500);
	WaitThreads();
}

BOOL VAAutomationTestInfo::GetTestInfo()
{
	long testPos = -1;
	WTString buf;

	for (;;)
	{
		try
		{
			int tries = 0;
			for (tries = 0; !g_currentEdCnt; tries++)
			{
				// in debug builds, this was an assert, and change of focus or delay fixed
				// the problem.  in release builds, the ast thread would exception out.
				PokeFindResults();
				if (!g_currentEdCnt && tries > 5)
				{
					if (gStressTest)
					{
						// cleared all find results to exit test
						throw WtException("Empty find results?");
					}

					// leave this message on AST thread so that user can interact with IDE
					WtMessageBox("NULL editor while trying to find testInfo", "Visual Assist AST", 0);
				}
			}

			// save and use local so that if g_currentEdCnt gets set to NULL on UI
			// thread (due to change of focus?), it doesn't kill this run
			EdCntPtr ed = g_currentEdCnt;
			if (!ed)
				throw WtException("g_currentEdCnt changed after loop");

			testPos = -1;
			buf.Empty();
			for (tries = 0;; tries++)
			{
				RunFromMainThread([&ed, &buf]() { buf = ed->GetBuf(); });
				if (buf.GetLength() == 0 && ed)
				{
					WaitThreads();
					if (ed != g_currentEdCnt)
						throw WtException("g_currentEdCnt changed after Wait");

					RunFromMainThread([&ed, &buf]() {
						ed->SendMessage(WM_COMMAND, VAM_GETBUF);
						buf = ed->GetBuf();
					});
				}
				m_testCode.Empty();
				long bIdx;
				RunFromMainThread([&ed, this, &bIdx]() {
					uint cp = ed->CurPos();
					m_testStartPos = (uint)ed->GetBufIndex((long)cp);
					mFile = ed->FileName();
					uint lp = ed->LinePos();
					bIdx = ed->GetBufIndex((long)lp);
				});

				testPos = buf.Find(VA_AUTOTES_STR, bIdx);
				if (testPos == -1)
				{
					if (tries)
					{
						ConsoleTrace("\r\n\r\nFailed to locate VA_AUTOTES_STR string in %s.  Retrying...\r\n",
						             WTString(mFile).c_str());

						if (gStressTest)
						{
							VAAutomation* pAst = dynamic_cast<VAAutomation*>(this);
							_ASSERTE(pAst);
							if (NULL != pAst)
							{
								ConsoleTrace("StressTest: closing all windows due to find failure \r\n");
								pAst->CloseAllWindowForStressTest(true);
							}
						}
						else
						{
							// leave this message on AST thread so that user can interact with IDE
							if (!(tries % 10) &&
							    IDYES != WtMessageBox(
							                 "Can't find \"VaAutotest:\" string at the current position.\r\n"
							                 "Why don't you double-click an item in the Find Results window.\r\n\r\n"
							                 "Should AST keep trying (testing will end if you select NO)?",
							                 "VA Automation", MB_YESNO))
							{
								return FALSE;
							}
						}
					}

					buf.Empty();
					PokeFindResults();
					ed = g_currentEdCnt;
					if (!ed)
						throw WtException("g_currentEdCnt NULL after poke");
				}
				else
					break;
			}

			Sleep(100);
			SendMessage(gVaMainWnd->GetSafeHwnd(), WM_NULL, 0, 0);
			if (!g_currentEdCnt)
				throw WtException("g_currentEdCnt NULL after SendMessage");

			if (ed != g_currentEdCnt)
				throw EdCntChangedException();

			RunFromMainThread([&ed, this]() { m_TestLine = ed->CurLine(); });
		}
		catch (const WtException& e)
		{
			VALOGEXCEPTION("AST:");
			WTString msg;
			const EdCntChangedException* pe = dynamic_cast<const EdCntChangedException*>(&e);
			if (nullptr != pe)
				msg.WTFormat("Warn: VAAutomationThread GetTestInfo - retrying. WtEx %s\r\n", e.GetDesc().c_str());
			else
				msg.WTFormat("ERROR: exception caught in VAAutomationThread GetTestInfo - retrying. WtEx %s\r\n",
				             e.GetDesc().c_str());
			Log(msg.c_str());
			ConsoleTraceDirect(msg);
			g_currentEdCnt = NULL;
			::Sleep(1500);
			continue;
		}
		catch (...)
		{
			VALOGEXCEPTION("AST:");
			const char* const msg = "ERROR: exception caught in VAAutomationThread GetTestInfo - retrying\r\n";
			Log(msg);
			ConsoleTrace(msg);
			g_currentEdCnt = NULL;
			::Sleep(1500);
			continue;
		}

		break;
	}

	//	if ((uint)testPos > (m_testStartPos + 160))
	//		_asm nop; // for debugging test navigation problems

	m_testName = TokenGetField(buf.c_str() + testPos + strlen(VA_AUTOTES_STR), ": \t\r\n").Wide();
	if (m_testName.GetLength() < 4 || !wt_isalpha(m_testName[0]))
	{
		ConsoleTrace("\r\n\r\nInvalid test case name at line %d in file %s.\r\n", m_TestLine,
		             WTString(Basename(mFile)).c_str());
		return TRUE;
	}

	vCatLog("Editor.Events", "VaEventAT Test ***********  '%s'", WTString(m_testName).c_str());
	if (m_testName.Find(CStringW(kRunAllTestsPrefix)) == 0)
	{
		// ConsoleTrace(" (skip RunAllTests directive)\r\n");
		return TRUE; // Don't look for <CODE></CODE>
	}

	// Prepend filename to make it unique.
	m_testName = ::Basename(mFile) + L"_" + m_testName;

	LPCSTR code = StrStrI(buf.c_str() + testPos, "<CODE");
	mImplicitHelpers = StrCmpNI(code, "<CODE:X", 7) != 0;
	m_InsertLineBelow = StrCmpNI(code, "<CODE:NoInsert", 14) != 0;
	if (code)
	{
		LPCSTR endCode = StrStrI(code, "</CODE>");
		LPCSTR startCode = StrStr(code, ">");
		if (startCode && endCode)
		{
			code = startCode + 1;
			m_testCode = WTString(code, ptr_sub__int(endCode, code));
			if (StrStrI(m_testCode.c_str(), "<code")) // Sanity check
			{
				ConsoleTrace("\r\n\r\nInvalid test case - contains <code> tag.\r\n");
				return FALSE;
			}

			m_testStartPos = ptr_sub__uint(endCode + strlen("</code>"), buf.c_str());
			return TRUE;
		}

		ConsoleTrace("\r\n\r\nInvalid test case - missing or incomplete tag.\r\n");
		return FALSE;
	}

	ConsoleTrace("%s has no code\r\n", WTString(m_testName).c_str());
	return TRUE;
}

bool VAAutomationTestInfo::AddTestRun()
{
	for (TestInfoV::iterator it = mTestInfo.begin(); it != mTestInfo.end(); ++it)
	{
		if ((*it).mTestName == m_testName)
		{
			mCurrentTestInfo = nullptr;
			return false;
		}
	}

	TestInfo inf = {m_testName, mFile, TestInfo::TestFail, true, m_TestLine};
	mTestInfo.push_back(inf);
	mCurrentTestInfo = &mTestInfo[mTestInfo.size() - 1];
	return !!mCurrentTestInfo;
}

// Returns the thread count of the current process or -1 in case of failure.
// https://stackoverflow.com/questions/3749668/how-to-query-the-thread-count-of-a-process-using-the-regular-windows-c-c-apis
int GetCurrentThreadCount()
{
	// first determine the id of the current process
	DWORD const id = GetCurrentProcessId();

	// then get a process list snapshot.
	HANDLE const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
	if (!snapshot || snapshot == INVALID_HANDLE_VALUE)
		return -1;

	// initialize the process entry structure.
	PROCESSENTRY32 entry = {0};
	entry.dwSize = sizeof(entry);

	// get the first process info.
	BOOL ret = true;
	ret = Process32First(snapshot, &entry);
	while (ret && entry.th32ProcessID != id)
		ret = Process32Next(snapshot, &entry);

	CloseHandle(snapshot);
	return ret ? (int)entry.cntThreads : -1;
}

void DumpProcessStats()
{
	DWORD handleCnt = 0;
	HANDLE hProc = ::GetCurrentProcess();
	::GetProcessHandleCount(hProc, &handleCnt);
	int thrdCnt = ::GetCurrentThreadCount();
	DWORD gdiObj = ::GetGuiResources(hProc, GR_GDIOBJECTS);
	DWORD userObj = ::GetGuiResources(hProc, GR_USEROBJECTS);
	DWORD gdiObjPeak = ::GetGuiResources(hProc, 2 /* GR_GDIOBJECTS_PEAK */);
	DWORD userObjPeak = ::GetGuiResources(hProc, 4 /* GR_USEROBJECTS_PEAK */);
	PROCESS_MEMORY_COUNTERS pmc;
	size_t workingSetSize = 0;
	size_t peakWorkingSetSize = 0;
	if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc)))
	{
		const int toMBs = 1024 * 1024;
		workingSetSize = pmc.WorkingSetSize / toMBs;
		peakWorkingSetSize = pmc.PeakWorkingSetSize / toMBs;
	}
	ConsoleTrace("Process stats: handles=%ld, threads=%d, GdiObjects=%ld (peak=%ld), UserObjects=%ld (peak=%lu), "
	             "Memory(MB)=%zu (peak=%zu)\r\n",
	             handleCnt, thrdCnt, gdiObj, gdiObjPeak, userObj, userObjPeak, workingSetSize, peakWorkingSetSize);
}

void VAAutomationTestInfo::GetResults(CString& duration, int& totalFail, int& totalMissingExpectedResult)
{
	const DWORD elapsedTime = ::GetTickCount() - m_StartTime;
	struct ResultCounts
	{
		ResultCounts()
		    : testsPassed(0), testsFailed(0), testsMissingExpectedResult(0)
		{
		}
		int testsPassed;
		int testsFailed;
		int testsMissingExpectedResult;
	};
	typedef std::map<int, ResultCounts> Results;
	Results results;
	bool failuresListDisplayed = false;
	CStringW lastFile;
	int totalPass = 0;
	totalFail = totalMissingExpectedResult = 0;
	for (TestInfoV::iterator it = mTestInfo.begin(); it != mTestInfo.end(); ++it)
	{
		TestInfo& inf = *it;
		ResultCounts& curResults = results[::GetFileType(inf.mTestSourceFile)];
		switch (inf.mState)
		{
		case TestInfo::TestFail:
			curResults.testsFailed++;
			totalFail++;
			if (!failuresListDisplayed)
			{
				failuresListDisplayed = true;
				ConsoleTrace("\r\n\r\nThere were test failures:\r\n");
			}

			// [case: 44794] report all failures
			if (lastFile != inf.mTestSourceFile)
			{
				ConsoleTrace("\r\n%ls\r\n", (LPCWSTR)inf.mTestSourceFile);
				lastFile = inf.mTestSourceFile;
			}

			ConsoleTrace("(%d): %ls\r\n", inf.mTestLine, (LPCWSTR)inf.mTestName);
			break;

		case TestInfo::TestPass:
			curResults.testsPassed++;
			totalPass++;
			break;

		case TestInfo::TestMissingExpectedResult:
			curResults.testsMissingExpectedResult++;
			totalMissingExpectedResult++;
			break;

		default:
			_ASSERTE(!"unhandled TestInfo::TestState case");
		}
	}

	if (g_AutomatedRun || results.size() > 1)
	{
		ConsoleTrace("\r\nRun Summary by file type:\r\n");
		for (Results::const_iterator it = results.begin(); it != results.end(); ++it)
		{
			int curFiletype = (*it).first;
			ResultCounts curResult = (*it).second;
			if (curResult.testsFailed || curResult.testsMissingExpectedResult)
			{
				ConsoleTrace("\t%s: %d pass, %d fail", ::GetFileType(curFiletype), curResult.testsPassed,
				             curResult.testsFailed);
				if (curResult.testsMissingExpectedResult)
					ConsoleTrace(", %d missing expected results", curResult.testsMissingExpectedResult);
			}
			else
				ConsoleTrace("\t%s: %d pass", ::GetFileType(curFiletype), curResult.testsPassed);
			ConsoleTrace("\r\n");
		}
	}

	if ((elapsedTime / 60000.0) > 60)
		CString__FormatA(duration, "%.2f hours", elapsedTime / 60000.0 / 60.0);
	else if ((elapsedTime / 1000) > 180)
		CString__FormatA(duration, "%.1f minutes", elapsedTime / 60000.0);
	else
		CString__FormatA(duration, "%lu seconds", elapsedTime / 1000);

	if (g_AutomatedRun || totalFail || totalMissingExpectedResult)
	{
		ConsoleTrace("\r\n%zu tests run in %s\r\n", mTestInfo.size(), (LPCTSTR)duration);
		ConsoleTrace("%d pass, %d fail", totalPass, totalFail);
		if (totalMissingExpectedResult)
			ConsoleTrace(", %d missing expected results", totalMissingExpectedResult);
		ConsoleTrace("\r\n");
	}
	else
	{
		_ASSERTE(totalPass == (int)mTestInfo.size());
		ConsoleTrace("\r\n%zu tests run in %s (%d pass)\r\n", mTestInfo.size(), (LPCTSTR)duration, totalPass);
	}

	if (g_AutomatedRun || mTestInfo.size() > 1)
		::DumpProcessStats();

	if (mVsExceptionReported)
		ConsoleTrace("ERROR: An exception was reported by Visual Studio at some point.  VS will only report exception "
		             "once per session.\r\n");
}

void VAAutomationTestInfo::CheckExceptionDlg(int maxWait /*= 1*/)
{
	Sleep(0);

	const int kSleepAmt = 250;
	for (int duration = 0; duration < maxWait; duration += kSleepAmt)
	{
		HWND wnd = GetForegroundWindow();
		// can a window be hidden if it is the ForegroundWindow?
		if (!wnd || !IsWindowVisible(wnd))
			continue;

		WTString wndText(GetWindowTextString(wnd));
		if (wndText != kShellNameUsedInCloseAll)
			continue;

		GUITHREADINFO inf;
		ZeroMemory(&inf, sizeof(GUITHREADINFO));
		inf.cbSize = sizeof(GUITHREADINFO);
		GetGUIThreadInfo(g_mainThread, &inf);
		if (inf.hwndActive != wnd)
			continue;

		wndText = GetWindowClassString(wnd);
		if (wndText != "#32770")
			continue;

		// Enumerate child windows looking for cls==Static and text=="An exception has*"
		for (HWND chld = ::GetWindow(wnd, GW_CHILD); chld; chld = GetWindow(chld, GW_HWNDNEXT))
		{
			wndText = GetWindowClassString(chld);
			if (wndText != "Static")
				continue;

			wndText = GetWindowTextString(chld);
			if (0 != wndText.Find("An exception has"))
			{
				if (0 != wndText.Find("One or more errors"))
					continue;
			}

			mVsExceptionReported = true;
			// Press escape to clear dialog, using Typomatic:: to preserve logging.
			Typomatic::TypeString("<esc>");
			Typomatic::TypeString("<esc>");

			WTString msg;
			msg.WTFormat("VS Exception dialog was displayed");
			throw WtException(msg);
		}

		if (1 == maxWait)
			return;

		Sleep(kSleepAmt);
	}
}

bool VAAutomation::GetReRunTestInfo()
{
	if (gStressTest)
		return false;

	if (!gReRun.get() || !gReRun->mAllowed)
		return false;

	if (!gReRun->mFailedTests.empty() || !gReRun->mPassedTests.empty())
	{
		TestInfoV tiv;

		struct cleanup
		{
			cleanup()
			{
			}
			~cleanup()
			{
				gReRun->mFailedTests.clear();
				gReRun->mPassedTests.clear();
				gReRun->mSrcLog.Empty();
			}
		} _do_cleanup;

		std::list<CStringW> srcFiles;

		// Build a list of files to process
		// Its content depends on name of executed AST batch test

		if (m_testName == kRunAllTestsInFile)
		{
			// list only current file
			srcFiles.push_back(mFirstFileName);
		}
		else if (GlobalProject)
		{
			auto add_files = [&srcFiles](const FileList& files) {
				for (FileList::const_iterator it = files.begin(); it != files.end(); ++it)
				{
					if (StrStrIW(it->mFilename, L"\\AutoTestLogs\\"))
						continue;

					if (StrStrIW(it->mFilename, L"\\ast.txt"))
						continue;

					srcFiles.push_back(it->mFilename);
				}
			};

			if (GlobalProject->IsBusy()) // may this happen here?
			{
				bool try_again = false;
				do
				{
					int count = 0;

					while (GlobalProject->IsBusy() && ++count < 100)
						Sleep(100);

					if (GlobalProject->IsBusy())
					{
						if (gReRun->mPrompts)
						{
							WTString msg(_T("GlobalProject is still busy after 10s.\r\n\r\n"));
							msg.append(
							    _T("Do you want to wait another 10s?\r\n\r\nClick [Yes] to wait, [No] to end re-run."));

							if (IDNO == WtMessageBox(msg, _T("GlobalProject is busy..."), MB_YESNO))
								return false;

							try_again = true;
						}
						else
						{
							ConsoleTraceDirect(_T("GlobalProject is still busy after 10s, ending re-run"));
							return false;
						}
					}
				} while (try_again && GlobalProject->IsBusy());
			}

			if (m_testName == kRunAllTestsInProject)
			{
				// list all files from project
				for (ProjectInfoPtr pip : GlobalProject->GetProjectForFile(mFirstFileName))
					add_files(pip->GetFiles());
			}
			else
			{
				// list all files from solution
				RWLockReader lck;
				add_files(GlobalProject->GetFilesSortedByName(lck));
			}
		}

		if (!gReRun->mOnlyListed)
		{
			//////////////////////////////////////////////////////
			// if not gReRun->only_listed, we must apply all
			// tests and exclude those already passed

#if defined(_DEBUG) || defined(VA_CPPUNIT)
			WTString missing_in_log; // tests not listed in TMP/LOG
#endif                               // defined(_DEBUG) || defined(VA_CPPUNIT)

			for (auto it = srcFiles.begin(); it != srcFiles.end(); ++it)
			{
				WTString src_code = ReadFile(*it);
				if (!src_code.IsEmpty())
				{
					int start_index = src_code.Find(_T(VA_AUTOTES_STR));
					if (start_index >= 0)
					{
						WTString baseName = Basename(WTString(*it));

						auto passed_it = gReRun->mPassedTests.find(baseName);
						auto failed_it = gReRun->mFailedTests.find(baseName);

#if defined(_DEBUG) || defined(VA_CPPUNIT)
						auto all_in_log_it = gReRun->mAllInLogDbg.find(baseName);
#endif // defined(_DEBUG) || defined(VA_CPPUNIT)

						VAAutomationReRun::FindTestsInSrc(
						    src_code.c_str() + start_index, true, true, [&](LPCTSTR test_s, LPCTSTR test_e) {
							    WTString test(test_s, ptr_sub__int(test_e, test_s));

#if defined(_DEBUG) || defined(VA_CPPUNIT)
							    if (all_in_log_it == gReRun->mAllInLogDbg.end() ||
							        all_in_log_it->second.find(test) == all_in_log_it->second.end())
							    {
								    if (!missing_in_log.IsEmpty())
									    missing_in_log.append(_T("\r\n"));

								    missing_in_log.append(baseName.c_str());
								    missing_in_log.append('_');
								    missing_in_log.append(test.c_str());
							    }
#endif // defined(_DEBUG) || defined(VA_CPPUNIT)

							    bool passed = passed_it != gReRun->mPassedTests.end() &&
							                  passed_it->second.find(test) != passed_it->second.end();

							    if (!passed)
							    {
								    if (failed_it == gReRun->mFailedTests.end())
									    failed_it = gReRun->mFailedTests.insert(
									        gReRun->mFailedTests.end(),
									        std::make_pair(baseName, VAAutomationReRun::TestNameSet()));

								    failed_it->second.insert(test);
							    }
						    });
					}
				}
			}

#ifdef _DEBUG
			_ASSERTE(missing_in_log.IsEmpty());
#endif // _DEBUG
		}

		/////////////////////////////////////////////
		// resolve tests full paths and line numbers

		size_t resolved_count_goal = 0;
		size_t resolved_count = 0;

		for (auto& kvp : gReRun->mFailedTests)
		{
			resolved_count_goal += kvp.second.size();

			// resolve test info list
			bool found_file = false;

			size_t resolved_count_local = 0;

			for (auto it = srcFiles.begin(); it != srcFiles.end(); ++it)
			{
				if (resolved_count_local == kvp.second.size())
					break;

				WTString baseName = Basename(WTString(*it));

				if (baseName.CompareNoCase(kvp.first) == 0)
				{
					found_file = true;

					WTString src_code = ReadFile(*it);

					if (src_code.Find(_T(VA_AUTOTES_STR)) >= 0)
					{
						LPCTSTR line_start =
						    src_code.c_str(); // first line (don't move line_start to first VA_AUTOTES_STR position)
						LPCTSTR line_end = nullptr;

						int line_num = 1;
						WTString line;

						while (line_start && *line_start)
						{
							if (resolved_count_local == kvp.second.size())
								break;

							line_end = _tcspbrk(line_start, _T("\r\n"));

							if (line_end)
							{
								if (line_end[0] == '\r' && line_end[1] == '\n')
									line_end += 2;
								else
									line_end++;

								line = WTString(line_start, ptr_sub__int(line_end, line_start));
							}
							else
							{
								// last line
								line = line_start;
							}

							static const int kVA_AUTOTES_STR_Len = strlen_i(_T(VA_AUTOTES_STR));
							int index = line.find(_T(VA_AUTOTES_STR));
							while (index >= 0)
							{
								index += kVA_AUTOTES_STR_Len; // move index to start of test name

								for (const WTString& str : kvp.second)
								{
									if (StrNCmp(line.c_str() + index, str.c_str(), str.GetLength()) == 0)
									{
										// Check if we have not found only a substring of test,
										// like test "testAbc" in another test "testAbc1"
										// Test name must end by ':' or by white space
										TCHAR end_char = line[index + str.GetLength()];
										if (end_char &&
										    !StrChr(kEOTCharsInSrc, (WORD)end_char)) // end_char could be '\0' in weird
										                                             // case, which is valid end of test
											continue;

										TestInfo ti = {Basename(*it) + L"_" +
										                   str.Wide(), // wide test name for proper search
										               *it, TestInfo::TestFail, true, line_num};

										tiv.push_back(ti);

										resolved_count++;
										resolved_count_local++;

										index += str.GetLength(); // move index behind this test

										break;
									}
								}

								// this should return -1 (if there is only 1 test per line)
								index = line.find(_T(VA_AUTOTES_STR), index);
								_ASSERTE(index == -1);
							}

							line_start = line_end;
							line_num++;
						}
					}
				}
			}

			if (!found_file)
			{
				_ASSERTE(!"No file with base in solution!");
				return false; // we could not resolve file name properly, perform full re-run
			}
			else
			{
				_ASSERTE(resolved_count_local == kvp.second.size());
			}
		}

		_ASSERTE(resolved_count_goal == resolved_count);

		mTestInfo = tiv;

		if (gReRun->mRelog)
		{
			// log contents of previous run
			ConsoleTraceDirect(_T("\r\n\r\n*******************************************************\r\n"));
			ConsoleTraceDirect(_T("************* <<<< Start of re-log >>>> ***************\r\n\r\n"));
			ConsoleTraceDirect(gReRun->mSrcLog.c_str());
			ConsoleTraceDirect(_T("\r\n\r\n************** <<<< End of re-log >>>> ****************\r\n"));
			ConsoleTraceDirect(_T("*******************************************************\r\n\r\n"));
		}

		return true;
	}

	return false;
}

void WINAPI DelayFileOpenCB(LPCWSTR file)
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	DelayFileOpen(file);
}

void InitVsSettings()
{
	// #astSetVsDefaults
	if (gShellAttr->IsDevenv10OrHigher())
	{
		g_IdeSettings->SetEditorOption("C/C++ Specific", "DisableIntelliSense", "FALSE");
		g_IdeSettings->SetEditorOption("C/C++", "InsertTabs", "1");

		if (gShellAttr->IsDevenv14OrHigher())
		{
			if (gShellAttr->IsDevenv16OrHigher())
			{
				g_IdeSettings->SetEditorOption("C/C++", "AutoListParams", "1");
				g_IdeSettings->SetEditorOption("C/C++", "BraceCompletion", "1");
				g_IdeSettings->SetEditorOption("C/C++", "VirtualSpace", "0");
				g_IdeSettings->SetEditorOption("C/C++ Specific", "AutoFormatOnClosingBrace", "1");
				g_IdeSettings->SetEditorOption("C/C++ Specific", "AutoFormatOnSemicolon", "0");
				g_IdeSettings->SetEditorOption("C/C++ Specific", "PointerAlignment", "0");

				g_IdeSettings->SetEditorOption("CSharp", "IndentStyle", "2");
				g_IdeSettings->SetEditorOption("CSharp", "BraceCompletion", "1");
				g_IdeSettings->SetEditorOption("CSharp", "VirtualSpace", "0");
				g_IdeSettings->SetEditorOption("CSharp-Specific", "NewLines_Braces_ControlFlow", "1");
				g_IdeSettings->SetEditorOption("CSharp-Specific", "Formatting_TriggerOnBlockCompletion", "1");
				g_IdeSettings->SetEditorOption("CSharp-Specific", "Formatting_TriggerOnStatementCompletion", "1");

				// additional items that might need to be automated:
				// Text Editor | General |
				//							Track changes -- off
				//							Follow project coding conventions -- off
				//			| C# | General
				//							Navigation bar -- off
				//							param info -- on
				//			| C++ | General
				//							nav bar -- off
			}
			else
				g_IdeSettings->SetEditorOption("CSharp-Specific", "NewLines_Braces_ControlFlow", "1");
		}
	}
}

void VAAutomation::ReRunFailedTests()
{
	_ASSERTE(!gStressTest);
	m_quit = FALSE;
	if (g_AutomatedRun)
		CloseAllDocuments();
	else
		RunVSCommand("Window.CloseAllDocuments", FALSE, FALSE); // Block until user presses y/n to save all?

	TestInfoV failedTests;
	for (TestInfoV::iterator it = mTestInfo.begin(); it != mTestInfo.end(); ++it)
		if (it->mState == TestInfo::TestFail)
			failedTests.push_back(*it);
	mTestInfo.clear();

	::Sleep(5000);
	if (gShellAttr->IsDevenv14OrHigher() && !gShellAttr->IsDevenv15u8OrHigher())
	{
		ConsoleTrace("Waiting extra long for VS2015-2017 (< 15.8)...\r\n");
		::Sleep(10000);
	}

	::InitVsSettings();

	ConsoleTrace("Rerun of failed tests...\r\n");
	CStringW lastRerunFile;
	int rerunFileCount = 0;
	for (TestInfoV::iterator it = failedTests.begin(); it != failedTests.end() && IsOK(); ++it)
	{
		bool ranCurTest = false;
		bool openedFile = false;
		try
		{
			if (lastRerunFile != it->mTestSourceFile)
			{
				if (!(rerunFileCount % kMaxFileOpenCount))
					CloseAllDocuments();

				lastRerunFile = it->mTestSourceFile;
				++rerunFileCount;
			}

			WaitThreads(TRUE);
			RunFromMainThread([&it]() { DelayFileOpen(it->mTestSourceFile); });
			WaitThreads(TRUE);
			openedFile = true;
			::Sleep(1000);
		}
		catch (...)
		{
			VALOGEXCEPTION("AST:");
			const char* const msg = "ERROR: exception caught in VAAutomationThread ReRunFailedTests DelayFileOpen - "
			                        "skipping current test\r\n";
			Log(msg);
			ConsoleTrace(msg);
		}

		if (openedFile)
		{
			bool foundTheRightTest = false;
			CStringW basename = Basename(it->mTestSourceFile);
			for (int attempts = 3; attempts--;)
			{
				if (!IdeFind(CString(VA_AUTOTES_STR) + CString(it->mTestName).Mid(basename.GetLength() + 1)))
					break;

				// confirm that the whole test name is correct
				// (that we didn't just hit a test that is similar;
				// like finding testnameFooA when looking for testnameFoo)
				GetTestInfo();
				if (m_testName == it->mTestName)
				{
					foundTheRightTest = true;
					break;
				}

				// change position and continue search
				EdCntPtr ed(g_currentEdCnt);
				RunFromMainThread([&ed]() {
					uint cp = ed->CurPos();
					ed->SetPos(cp + 4);
				});
			}

			if (foundTheRightTest)
			{
				try
				{
					if (RunTestAtCaret())
						ranCurTest = true;
					else
					{
						CString msg;
						CString__FormatA(msg,
						                 "ERROR: execution failure of failed test rerun -- check for duplicate test "
						                 "name %ls in file\r\n  %ls\r\n",
						                 (LPCWSTR)it->mTestName, (LPCWSTR)it->mTestSourceFile);
						Log((const char*)msg);
						ConsoleTraceDirect(msg);
					}
				}
				catch (...)
				{
					VALOGEXCEPTION("AST:");
					CString msg;
					CString__FormatA(msg,
					                 "ERROR: exception caught in VAAutomationThread ReRunFailedTests RunTestAtCaret - "
					                 "skipping current test\r\n  %ls\r\n  %ls\r\n",
					                 (LPCWSTR)it->mTestName, (LPCWSTR)it->mTestSourceFile);
					Log((const char*)msg);
					ConsoleTraceDirect(msg);
					::Sleep(1000);
				}
			}
			else
			{
				ConsoleTrace("Find failed - skipping test in\r\n  %ls\r\n  active: %ls\r\n  ed: %p\r\n",
				             (LPCWSTR)it->mTestSourceFile, GetActiveDocumentSafe(), g_currentEdCnt.get());
			}
		}

		if (!ranCurTest)
		{
			// requeue the test
			ConsoleTrace("Test failed to run - requeue test %ls\r\n", (LPCWSTR)it->mTestName);

			m_testName = it->mTestName;
			mFile = it->mTestSourceFile;
			m_TestLine = it->mTestLine;
			AddTestRun();
			mCurrentTestInfo = NULL;
		}
	}
}

void VAAutomationOptions::TypeString(LPCSTR code, BOOL checkOk)
{
	// Set defaults
	*Psettings = m_defaultSettings;
	RunFromMainThread([] { ::VaOptionsUpdated(); });
	m_VsSettings.RestoreOptions();

	try
	{
		__super::TypeString(code, checkOk);
	}
	catch (const WtException& e)
	{
		WTString msg;
		msg.WTFormat("\r\nException caught in VAAutomationOptions::TypeString during __super::TypeString: %s\r\n",
		             e.GetDesc().c_str());
		Log(msg.c_str());
		ConsoleTraceDirect(msg);
	}

	try
	{
		WaitThreads();
	}
	catch (const WtException& e)
	{
		WTString msg;
		msg.WTFormat("\r\nException caught in VAAutomationOptions::TypeString during WaitThreads: %s\r\n",
		             e.GetDesc().c_str());
		Log(msg.c_str());
		ConsoleTraceDirect(msg);
	}

	if (!Psettings->m_enableVA)
	{
		RunVSCommand("VAssistX.EnableDisable");
		Sleep(500); // Give it a chance to update
	}

	// Restore user settings
	*Psettings = m_orgSettings;
	RunFromMainThread([] { ::VaOptionsUpdated(); });
}

void VAAutomationOptions::ProcessControlCharacters(WTString controlTag)
{
	WTString wtmodKeys = TokenGetField(controlTag, "+");
	LPCSTR modKeys = wtmodKeys.c_str();
	if (StrStrI(modKeys, "<ToggleVA"))
	{
		RunVSCommand("VAssistX.EnableDisable");
		Sleep(500); // Give it a chance to update
	}
	else if (StrStrI(modKeys, "<InvalidateBuf"))
	{
		WaitThreads();
		if (g_currentEdCnt)
			g_currentEdCnt->SendMessage(WM_COMMAND, VAM_GETBUF);
	}
	else if (StrStrI(modKeys, "<AutoMatch"))
		Psettings->AutoMatch = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AutoDisplayRefactoringButton"))
		Psettings->mAutoDisplayRefactoringButton = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<IncludeDefaultParameterValues"))
		Psettings->mIncludeDefaultParameterValues = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<CompleteWithAnyVsOverride"))
		Psettings->m_bCompleteWithAnyVsOverride = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<CompleteWithAny"))
		Psettings->m_bCompleteWithAny = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<CompleteWithTab"))
		Psettings->m_bCompleteWithTab = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<CompleteWithReturn"))
		Psettings->m_bCompleteWithReturn = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MembersCompleteWithAny"))
	{
		if (!gShellAttr->IsDevenv11OrHigher())
			Psettings->mMembersCompleteOnAny = GetBoolArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<ListboxSelectionStyle"))
		Psettings->mSuggestionSelectionStyle = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<CompletionOverwriteBehavior"))
		Psettings->mListboxOverwriteBehavior = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<CaseCorrect"))
		Psettings->CaseCorrect = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SurroundingBits"))
		Psettings->m_defGuesses = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<FilterSuggestionsLists"))
		Psettings->m_UseVASuggestionsInManagedCode = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<IncludeSmartSuggestions"))
		Psettings->mScopeSuggestions = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<ExtendCommentOnNewline"))
		Psettings->mExtendCommentOnNewline = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<ListNonInheritedMembersFirst"))
		Psettings->m_bListNonInheritedMembersFirst = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<UseDefaultIntellisense"))
		Psettings->m_bUseDefaultIntellisense = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListShowParams"))
		Psettings->mParamsInMethodsInFileList = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListShowRegions"))
		Psettings->mMethodInFile_ShowRegions = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListShowScope"))
		Psettings->mMethodInFile_ShowScope = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<NavBarContextLimitScope"))
		Psettings->mNavBarContext_DisplaySingleScope = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListShowDefines"))
		Psettings->mMethodInFile_ShowDefines = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListShowEvents"))
		Psettings->mMethodInFile_ShowEvents = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListShowProperties"))
		Psettings->mMethodInFile_ShowProperties = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListShowMembers"))
		Psettings->mMethodInFile_ShowMembers = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<MethodsInFileListNamespaceFilter"))
		Psettings->mMethodsInFileNameFilter = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<ConvertDotToPtr"))
		Psettings->m_fixPtrOp = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<ConvertDotToPtrIfOverloaded"))
		Psettings->m_fixSmartPtrOp = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<BraceAutoMatchStyle"))
		Psettings->mBraceAutoMatchStyle = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<ClosingBraceSpaces"))
		Psettings->m_fnParenGap = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<ClearTypingHistory"))
	{
		g_rbuffer.Clear();
		if (g_ExpHistory)
			g_ExpHistory->Clear();
		g_Guesses.Reset();
		g_Guesses.ClearMostLikely();
	}
	else if (StrStrI(modKeys, "<VaSnippets"))
		Psettings->m_codeTemplateTooltips = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<VaSuggestions"))
		Psettings->m_autoSuggest = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AddIncludeStyle"))
		Psettings->mAddIncludeStyle = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<ShrinkWhenPossible"))
		Psettings->m_bShrinkMemberListboxes = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AllowAcronymsAndShorthand"))
		Psettings->m_bAllowShorthand = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<PreferAcronymMatches"))
		Psettings->m_bAllowAcronyms = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AddIncludeDelimiter"))
	{
		switch (GetArgValue(controlTag))
		{
		default:
		case 0:
			Psettings->mDefaultAddIncludeDelimiter = L'\\';
			break;
		case 1:
			Psettings->mDefaultAddIncludeDelimiter = L'/';
			break;
		}
	}
	else if (StrStrI(modKeys, "<OldUncommentBehavior"))
		Psettings->mOldUncommentBehavior = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<TemplateMoveToSourceInTwoSteps"))
		Psettings->mTemplateMoveToSourceInTwoSteps = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<CommentWithSpace"))
		Psettings->mDontInsertSpaceAfterComment = !GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<DismissSuggestionListOnUpDown"))
		Psettings->mDismissSuggestionListOnUpDown = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<ResizeSuggestionListOnUpDown"))
		Psettings->mResizeSuggestionListOnUpDown = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SurroundWithSnippetOnCharIgnoreWhitespace"))
		Psettings->mSurroundWithSnippetOnCharIgnoreWhitespace = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SmartPtrSuggestStyles"))
		Psettings->mSmartPtrSuggestModes = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<FunctionCallParenStyle"))
	{
		switch (GetArgValue(controlTag))
		{
		default:
		case 0:
			::strcpy(Psettings->mFunctionCallParens, "()");
			break;
		case 1:
			::strcpy(Psettings->mFunctionCallParens, "( )");
			break;
		case 2:
			::strcpy(Psettings->mFunctionCallParens, "(  )");
			break;
		}
	}
	else if (StrStrI(modKeys, "<SelectImplementation"))
		Psettings->mSelectImplementation = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<QuickInfoTooltips"))
	{
		Psettings->m_mouseOvers = GetBoolArgValue(controlTag);
		if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
			Psettings->mQuickInfoInVs8 = GetBoolArgValue(controlTag);
		Psettings->CheckForConflicts();
	}
	else if (StrStrI(modKeys, "<CommentsInTooltips"))
	{
		Psettings->m_AutoComments = GetBoolArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<SurroundSelectionChars"))
	{
		strcpy(Psettings->mSurroundWithKeys, GetStringArgValue(controlTag).c_str());
	}
	else if (StrStrI(modKeys, "<SourceCommentsInTooltips"))
	{
		Psettings->m_bGiveCommentsPrecedence = GetBoolArgValue(controlTag);
		Psettings->CheckForConflicts();
	}
	else if (StrStrI(modKeys, "<ContextTooltips"))
	{
		Psettings->mScopeTooltips = GetBoolArgValue(controlTag);
		Psettings->CheckForConflicts();
	}
	else if (StrStrI(modKeys, "<ClearMarkers>"))
	{
		::ClearAutoHighlights();
		g_ScreenAttrs.Invalidate();
		WaitThreads();
	}
	else if (StrStrI(modKeys, "<VSOption"))
	{
		// <VSOption:lang:prop:val> i.e. <VSOption:HTML Specific:AttrValueNotQuoted:0>
		token2 t(controlTag);
		t.read(':');
		WTString lang = t.read(':');
		WTString prop = t.read(':');
		WTString val = t.read(':');
		if (t.more())
		{
			// [case: 63366] if the val is supposed to have a colon in it, the read chopped it
			val += t.Str();
		}
		m_VsSettings.SetOption(lang, prop, val);
	}
	else if (StrStrI(modKeys, "<SetACP:"))
	{
		token2 t(controlTag);
		t.read(':');
		UINT acp = (UINT)atoi(t.read().c_str());
		m_ACP = SpoofACP(acp);
		if (g_currentEdCnt)
			g_currentEdCnt->SendMessage(WM_COMMAND, VAM_GETBUF);
	}
	else if (StrStrI(modKeys, "<RefactorAutoFormat:"))
	{
		_ASSERTE(gShellAttr->IsDevenv8OrHigher());
		Psettings->mRefactorAutoFormat = GetBoolArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<SuppressVaListboxes:"))
		Psettings->mSuppressAllListboxes = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AugmentParamInfo:"))
		Psettings->mVaAugmentParamInfo = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<PartialSnippetShortcutMatches:"))
		Psettings->mPartialSnippetShortcutMatches = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<RestrictVaToPrimaryLangs:"))
		Psettings->mRestrictVaToPrimaryFileTypes = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<RestrictVaListboxesToC:"))
		Psettings->mRestrictVaListboxesToC = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<EditUserInputFieldsInEditor:"))
		Psettings->mEditUserInputFieldsInEditor = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<UseCppOverrideKeyword:"))
		Psettings->mUseCppOverrideKeyword = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<UseCppVirtualKeyword:"))
		Psettings->mUseCppVirtualKeyword = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<VaViewMru:"))
		Psettings->m_nMRUOptions = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<SuggestNullptr:"))
		Psettings->mSuggestNullptr = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SuggestNull:"))
		Psettings->mSuggestNullInCppByDefault = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<HCB_UpdateOnHover:"))
		Psettings->mUpdateHcbOnHover = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SmartSelectEnableGranularStart:"))
		Psettings->mSmartSelectEnableGranularStart = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SmartSelectEnableWordStart:"))
		Psettings->mSmartSelectEnableWordStart = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SmartSelectSplitWordByCase:"))
		Psettings->mSmartSelectSplitWordByCase = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<SmartSelectSplitWordByUnderscore:"))
		Psettings->mSmartSelectSplitWordByUnderscore = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<Insert_onMshift:"))
		Psettings->m_auto_m_ = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<HCB_UpdateOnScope:"))
	{
		int newVal = GetArgValue(controlTag);
		if ((newVal && !(Psettings->m_nHCBOptions & 1)) || (!newVal && (Psettings->m_nHCBOptions & 1)))
			Psettings->m_nHCBOptions ^= 1;
	}
	else if (StrStrI(modKeys, "<HCB_UpdateOnPosition:"))
	{
		int newVal = GetArgValue(controlTag);
		if ((newVal && !(Psettings->m_nHCBOptions & 2)) || (!newVal && (Psettings->m_nHCBOptions & 2)))
			Psettings->m_nHCBOptions ^= 2;
	}
	else if (StrStrI(modKeys, "<AutoHighlightRefs:"))
	{
		Psettings->mAutoHighlightRefs = GetBoolArgValue(controlTag);
		m_VsSettings.SetOption("CSharp-Specific", "HighlightReferences", Psettings->mAutoHighlightRefs ? "0" : "1");
		m_VsSettings.SetOption("Basic-Specific", "EnableHighlightReferences",
		                       Psettings->mAutoHighlightRefs ? "FALSE" : "TRUE");
		if (gShellAttr->IsDevenv11OrHigher())
			m_VsSettings.SetOption("C/C++ Specific", "DisableReferenceHighlighting",
			                       Psettings->mAutoHighlightRefs ? "TRUE" : "FALSE");
	}
	else if (StrStrI(modKeys, "<DeleteFile:"))
	{
		// <DeleteFile:filename>
		CStringW file(GetStringArgValue(controlTag).Wide());
		if (file.IsEmpty())
			return;

		// convert to absolute path
		EdCntPtr ed(g_currentEdCnt);
		if (!ed)
			return;

		CStringW curPath(::Path(ed->FileName()));
		file = curPath + L"\\" + file;

		if (g_ParserThread)
			g_ParserThread->QueueParseWorkItem(new SymbolRemover(file));

		if (::IsFile(file))
			::DeleteFileW(file);
	}
	else if (StrStrI(modKeys, "<CopyFile:"))
	{
		// <CopyFile:filenameFrom;filenameTo>
		const CStringW files(GetStringArgValue(controlTag).Wide());
		if (files.IsEmpty())
		{
			ConsoleTrace("ERROR: no arguments to CopyFile\r\n");
			return;
		}

		int pos = 0;
		CStringW file1 = files.Tokenize(L";", pos);
		CStringW file2 = files.Tokenize(L";", pos);
		if (file1.IsEmpty() || file2.IsEmpty())
		{
			ConsoleTrace("ERROR: empty file argument to CopyFile\r\n");
			return;
		}

		// convert to absolute path
		EdCntPtr ed(g_currentEdCnt);
		if (!ed)
			return;

		const CStringW curPath(::Path(ed->FileName()) + L"\\");
		file1 = ::MSPath(curPath + file1);
		file2 = ::MSPath(curPath + file2);
		if (!::IsFile(file1))
		{
			ConsoleTrace("ERROR: CopyFile source file not found\r\n");
			return;
		}

		const CStringW pth2(::Path(file2));
		if (!::IsDir(pth2))
			::CreateDir(pth2);

		::CopyFileW(file1, file2, TRUE);
	}
	else if (StrStrI(modKeys, "<FileAttrReadOnly:"))
	{
		CStringW file(GetStringArgValue(controlTag).Wide());
		if (file.IsEmpty())
			return;

		// convert to absolute path
		EdCntPtr ed(g_currentEdCnt);
		if (!ed)
			return;

		CStringW curPath(::Path(ed->FileName()));
		file = curPath + L"\\" + file;

		// confirm that it is a file and not a directory
		const DWORD attribs = GetFileAttributesW(file);
		if (attribs == INVALID_FILE_ATTRIBUTES || (attribs & FILE_ATTRIBUTE_DIRECTORY))
			return; // not a file

		// add read-only attribute if not included
		if (!(attribs & FILE_ATTRIBUTE_READONLY))
			SetFileAttributesW(file, attribs | FILE_ATTRIBUTE_READONLY);
	}
	else if (StrStrI(modKeys, "<FileAttrReadWrite:"))
	{
		CStringW file(GetStringArgValue(controlTag).Wide());
		if (file.IsEmpty())
			return;

		// convert to absolute path
		EdCntPtr ed(g_currentEdCnt);
		if (!ed)
			return;

		CStringW curPath(::Path(ed->FileName()));
		file = curPath + L"\\" + file;

		// confirm that it is a file and not a directory
		const DWORD attribs = GetFileAttributesW(file);
		if (attribs == INVALID_FILE_ATTRIBUTES || (attribs & FILE_ATTRIBUTE_DIRECTORY))
			return; // not a file

		// remove read-only attribute if it is included
		if (attribs & FILE_ATTRIBUTE_READONLY)
			SetFileAttributesW(file, attribs & ~FILE_ATTRIBUTE_READONLY);
	}
	else if (StrStrI(modKeys, "<LinesBetweenMethods:"))
	{
		Psettings->mLinesBetweenMethods = (DWORD)GetArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<FindSimilarLocation:"))
	{
		Psettings->mFindSimilarLocation = GetBoolArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<InsertOpenBraceOnNewLine:"))
	{
		Psettings->mInsertOpenBraceOnNewLine = GetBoolArgValue(controlTag);
		m_VsSettings.SetOption("CSharp-Specific", "NewLines_Braces_ControlFlow",
		                       Psettings->mInsertOpenBraceOnNewLine ? "1" : "0");
	}
	else if (StrStrI(modKeys, "<EncFieldPublicAccessors:"))
	{
		Psettings->mEncFieldPublicAccessors = GetBoolArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<EnableSortLinesPrompt:"))
	{
		Psettings->mEnableSortLinesPrompt = GetBoolArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<DisplayHashtagXrefs:"))
	{
		Psettings->mDisplayHashtagXrefs = GetBoolArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<GotoOverloadResolutionMode:"))
	{
		Psettings->mGotoOverloadResolutionMode = GetArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<GotoRelatedOverloadResolutionMode:"))
	{
		Psettings->mGotoRelatedOverloadResolutionMode = GetArgValue(controlTag);
	}
	else if (StrStrI(modKeys, "<UnrealSupport:"))
	{
		Psettings->mUnrealEngineCppSupport = GetBoolArgValue(controlTag);
		// FreeDFileMps could cause a crash if ui thread is coloring while ast is running.
		// See if these waits are enough to prevent crashes.
		::Sleep(1000);
		::AstSyncVa();
		::WaitThreads(FALSE);

		DatabaseDirectoryLock l;
		::FreeDFileMPs();
		::GetDFileMP(Src);

		const CStringW stdafx(VaDirs::GetParserSeedFilepath(L"StdafxUnreal.h"));
		if (::IsFile(stdafx))
		{
			MultiParsePtr mp = MultiParse::Create(Header);
			if (Psettings->mUnrealEngineCppSupport)
			{
				// UE4 specific defs
				if (!mp->IsIncluded(stdafx))
					mp->FormatFile(stdafx, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);
			}
			else
			{
				if (mp->IsIncluded(stdafx))
					mp->RemoveAllDefs(stdafx, DTypeDbScope::dbSlnAndSys);
			}
		}

		if (GlobalProject->GetUnrealEngineVersion() == L"5.0")
		{
			// [case: 149728] UE5.0 specific defs
			const CStringW stdafxUE50(VaDirs::GetParserSeedFilepath(L"StdafxUnreal50.h"));
			if (::IsFile(stdafxUE50))
			{
				MultiParsePtr mp = MultiParse::Create(Header);
				if (Psettings->mUnrealEngineCppSupport)
				{
					if (!mp->IsIncluded(stdafxUE50))
						mp->FormatFile(stdafxUE50, V_SYSLIB | V_VA_STDAFX, ParseType_Globals);
				}
				else
				{
					if (mp->IsIncluded(stdafxUE50))
						mp->RemoveAllDefs(stdafxUE50, DTypeDbScope::dbSlnAndSys);
				}
			}
		}
	}
	else if (StrStrI(modKeys, "<ClearNonSourceFileMatches:"))
		Psettings->mClearNonSourceFileMatches = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<GotoRelatedParameterTrimming"))
		Psettings->mGotoRelatedParameterTrimming = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<EnableFindRefsFlagCreation"))
		Psettings->mFindRefsFlagCreation = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<IncludeDirectiveCompletion"))
		Psettings->mIncludeDirectiveCompletionLists = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AllowHyphenInHashtag"))
		Psettings->mHashtagsAllowHypens = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<HoveringPopupButton"))
	{
		RunFromMainThread([controlTag] {
			Psettings->mDisplayRefactoringButton = GetBoolArgValue(controlTag);
			::VaOptionsUpdated();
		});
	}
	else if (StrStrI(modKeys, "<JavaDocStyle"))
		Psettings->mJavaDocStyle = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<RestrictFilesOpenedDuringRefactor"))
		Psettings->mRestrictFilesOpenedDuringRefactor = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<AllowSnippetsInUnrealMarkup:"))
		Psettings->mAllowSnippetsInUnrealMarkup = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AugmentSolutionFileList:"))
		Psettings->mOfisAugmentSolution = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<UseGitRootForAugmentSolution:"))
		Psettings->mUseGitRootForAugmentSolution = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<DoesContainUnrealEngineProject:"))
		GlobalProject->SetContainsUnrealEngineProject(GetBoolArgValue(controlTag));
	else if (StrStrI(modKeys, "<FilterGeneratedSourceFiles:"))
		Psettings->mFilterGeneratedSourceFiles = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<DisplayPersistentFilter:"))
		Psettings->mOfisDisplayPersistentFilter = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<ApplyPersistentFilter:"))
		Psettings->mOfisApplyPersistentFilter = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AddIncludeSkipFirstFileLevelComment:"))
		Psettings->mAddIncludeSkipFirstFileLevelComment = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AddIncludePathPreference:"))
		Psettings->mAddIncludePath = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<JumpToImpl:"))
		Psettings->mEnableJumpToImpl = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<FilterWithOverloads:"))
		Psettings->mEnableFilterWithOverloads = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<IndexPluginsOpt:"))
		Psettings->mIndexPlugins = (DWORD)GetArgValue(controlTag);
	else if (StrStrI(modKeys, "<IndexGeneratedCode:"))
		Psettings->mIndexGeneratedCode = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<DisableUeAutoformatOnPaste:"))
		Psettings->mDisableUeAutoforatOnPaste = GetBoolArgValue(controlTag);
	else if (StrStrI(modKeys, "<AddIncludeUnrealUseQuotation:"))
		Psettings->mAddIncludeUnrealUseQuotation = GetBoolArgValue(controlTag);
	else
		__super::ProcessControlCharacters(controlTag);
}

VAAutomationOptions::VAAutomationOptions()
{
	Psettings->mRestrictVaToPrimaryFileTypes = false;
	m_orgSettings = *Psettings;
	m_defaultSettings = *Psettings;
	m_defaultSettings.InitDefaults(NULL);
	// override default #astOverrideVaDefaults
	m_defaultSettings.mRestrictVaListboxesToC = false;
	m_defaultSettings.mRestrictVaToPrimaryFileTypes = false;
	m_defaultSettings.mUseCppOverrideKeyword = false;
	m_defaultSettings.mUseCppVirtualKeyword = true;
	m_defaultSettings.mEditUserInputFieldsInEditor = false;
	m_defaultSettings.mNavBarContext_DisplaySingleScope = false;          // [case: 91507] changed default behavior
	m_defaultSettings.mSurroundWithSnippetOnCharIgnoreWhitespace = false; // [case: 93760] changed default value
	m_defaultSettings.mFindRefsFlagCreation = true;                       // [case: 1262] necessary until 1262 is enabled by default
	if (gShellAttr && !gShellAttr->IsDevenv10OrHigher() && ::GetSystemMetrics(SM_REMOTESESSION))
		m_defaultSettings.mOptimizeRemoteDesktop = true;
	m_ACP = NULL;
}

void VAAutomationControl::ProcessControlCharacters(WTString controlTag)
{
	vCatLog("Editor.Events", "VaEventAT   ProcessControlCharacters '%s'", controlTag.c_str());
	const WTString wtmodKeys = TokenGetField(controlTag, "+");
	LPCSTR modKeys = wtmodKeys.c_str();
	if (StrStrI(modKeys, "<ListboxTextExists")) // <ListboxTextExists:text>
	{
		WTString logMsg;
		WaitForListBox();
		if (g_CompletionSet && g_CompletionSet->IsExpUp(g_currentEdCnt))
		{
			BOOL selectionState = FALSE;
			WTString listboxcontent;
			WaitUiThreadEvents();
			SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_THREAD_AST_GETLIST, (WPARAM)&listboxcontent, (LPARAM)selectionState);
			listboxcontent = "\r\n" + listboxcontent + "\r\n";

			const WTString testText(wtmodKeys.Mid(19));
			WTString txtToFind(testText);
			txtToFind = "\r\n" + txtToFind + "\r\n";
			txtToFind.ReplaceAll("\\001", "\001");
			txtToFind.ReplaceAll("\\002", "\002");
			txtToFind.ReplaceAll("\\003", "\003");
			if (-1 == listboxcontent.Find(txtToFind))
				logMsg.WTFormat("Listbox does not contain '%s'", testText.c_str());
			else
				logMsg.WTFormat("Listbox contains '%s'", testText.c_str());
		}
		else
			logMsg = "No listbox present.";

		LogStr(logMsg);
		return;
	}
	else if (StrStrI(modKeys, "HideMiniHelp"))
	{
		RunFromMainThread([] {
			Psettings->m_noMiniHelp = true;
			::VaOptionsUpdated();
		});
	}
	else if (StrStrI(modKeys, "<NormalSnippetExpansion:"))
	{
		EnableNormalSnippetExpansion(GetBoolArgValue(controlTag));
	}
	else if (StrStrI(modKeys, "<Prompt:"))
	{
		if (g_AutomatedRun)
			throw WtException("'Prompt' command is not allowed in automated run");

		WTString args(GetStringArgValue(controlTag));
		args.ReplaceAll("\\r", "\r");
		args.ReplaceAll("\\n", "\n");
		LogStr("Prompt: " + args);
		if (::WtMessageBox(nullptr, args, "AST Prompt From Test", MB_YESNO | MB_ICONQUESTION) == IDYES)
			LogStr("Response: Yes");
		else
			LogStr("Response: No");
		return;
	}
	else if (StrStrI(modKeys, "<MessageBox:"))
	{
		if (g_AutomatedRun)
			throw WtException("'MessageBox' command is not allowed in automated run");

		WTString args(GetStringArgValue(controlTag));
		args.ReplaceAll("\\r", "\r");
		args.ReplaceAll("\\n", "\n");
		::WtMessageBox(nullptr, args, "AST Message From Test", MB_OK | MB_ICONINFORMATION);
		return;
	}
	else if (StrStrI(modKeys, "<SetMouseWheelCommand:"))
	{
		CString args(GetStringArgValue(controlTag).c_str());

		if (args.IsEmpty())
		{
			ConsoleTrace("ERROR: Empty arguments in SetMouseWheelCommand\r\n");
			return;
		}

		int pos = 0;
		CString shortcut_str = args.Tokenize(":", pos);
		CString action_str = args.Tokenize(":", pos);
		DWORD shortcut = UINT_MAX;
		DWORD action = UINT_MAX;

		struct dword_key_value
		{
			LPCSTR key;
			DWORD value;
		};

		dword_key_value shortcuts[] = {
		    {"CtrlWheel", (DWORD)VaMouseWheelCmdBinding::CtrlMouseWheel},
		    {"CtrlShiftWheel", (DWORD)VaMouseWheelCmdBinding::CtrlShiftMouseWheel},
		};

		dword_key_value actions[] = {
		    {"None", (DWORD)VaMouseWheelCmdAction::None},
		    {"Zoom", (DWORD)VaMouseWheelCmdAction::Zoom},
		    {"Selection", (DWORD)VaMouseWheelCmdAction::Selection},
		    {"BlockSelection", (DWORD)VaMouseWheelCmdAction::BlockSelection},
		};

		for (auto shr : shortcuts)
		{
			if (shortcut_str.CompareNoCase(shr.key) == 0)
			{
				shortcut = shr.value;
				break;
			}
		}

		for (auto act : actions)
		{
			if (action_str.CompareNoCase(act.key) == 0)
			{
				action = act.value;
				break;
			}
		}

		_ASSERTE(action != UINT_MAX && shortcut != UINT_MAX);

		Psettings->mMouseWheelCmds.set(shortcut, action);
		if (gVaInteropService)
		{
			WaitThreads(false);
			RunFromMainThread([]() { gVaInteropService->OptionsUpdated(); });
		}
	}
	else if (StrStrI(modKeys, "<SetMouseCommand:"))
	{
		CString args(GetStringArgValue(controlTag).c_str());

		if (args.IsEmpty())
		{
			ConsoleTrace("ERROR: Empty arguments in SetMouseCommand\r\n");
			return;
		}

		int pos = 0;
		CString shortcut_str = args.Tokenize(":", pos);
		CString action_str = args.Tokenize(":", pos);
		DWORD shortcut = UINT_MAX;
		DWORD action = UINT_MAX;

		struct dword_key_value
		{
			LPCSTR key;
			DWORD value;
		};

		dword_key_value shortcuts[] = {
		    {"CtrlLeftClick", (DWORD)VaMouseCmdBinding::CtrlLeftClick},
		    {"AltLeftClick", (DWORD)VaMouseCmdBinding::AltLeftClick},
		    {"MiddleClick", (DWORD)VaMouseCmdBinding::MiddleClick},
		    {"ShiftRightClick", (DWORD)VaMouseCmdBinding::ShiftRightClick},
		};

		dword_key_value actions[] = {
		    {"None", (DWORD)VaMouseCmdAction::None},
		    {"Goto", (DWORD)VaMouseCmdAction::Goto},
		    {"SuperGoto", (DWORD)VaMouseCmdAction::SuperGoto},
		    {"ContextMenu", (DWORD)VaMouseCmdAction::ContextMenu},
		    {"ContextMenuOld", (DWORD)VaMouseCmdAction::ContextMenuOld},
		    {"RefactorCtxMenu", (DWORD)VaMouseCmdAction::RefactorCtxMenu},
		};

		for (auto shr : shortcuts)
		{
			if (shortcut_str.CompareNoCase(shr.key) == 0)
			{
				shortcut = shr.value;
				break;
			}
		}

		for (auto act : actions)
		{
			if (action_str.CompareNoCase(act.key) == 0)
			{
				action = act.value;
				break;
			}
		}

		_ASSERTE(action != UINT_MAX && shortcut != UINT_MAX);

		Psettings->mMouseClickCmds.set(shortcut, action);
		if (gVaInteropService)
		{
			WaitThreads(false);
			RunFromMainThread([]() { gVaInteropService->OptionsUpdated(); });
		}
	}
	else if (StrStrI(modKeys, "<LOG"))
	{
		if (StrStrI(modKeys, "DisableFinalState"))
		{
			EnableFinalStateLogging(false);
			return;
		}
		if (StrStrI(modKeys, "Disable"))
		{
			EnableLogging(false);
			return;
		}
		if (StrStrI(modKeys, "Enable"))
		{
			EnableLogging(true);
			return;
		}
		if (StrStrI(modKeys, "clipboardRtf"))
		{
			WTString rtf(::GetClipboardRtf(HWND_DESKTOP));
			rtf.Replace(" \\fs15 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs16 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs17 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs18 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs19 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs20 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs21 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs22 ", " \\fs(size removed) ");
			rtf.Replace(" \\fs23 ", " \\fs(size removed) ");

			rtf.Replace(" Consolas;", " (font removed);");
			rtf.Replace(" Courier;", " (font removed);");
			rtf.Replace(" Courier New;", " (font removed);");
			rtf.Replace(" Lucida Console;", " (font removed);");
			rtf.Replace(" Lucida Sans Typewriter;", " (font removed);");
			LogStr(WTString("clipboard rtf contents:\r\n") + rtf + WTString("\r\n"));
			return;
		}
		if (StrStrI(modKeys, "clipboard"))
		{
			const CStringW txtW(::GetClipboardText(HWND_DESKTOP));
			WTString msg;
			msg.WTFormat("clipboard contents:\r\n%s\r\n", WTString(txtW).c_str());
			LogStr(msg);
			return;
		}
		if (StrStrI(modKeys, "StripLineNumbers"))
		{
			EnableLineNumberStripping(true);
			return;
		}
		if (StrStrI(modKeys, "MenuStart"))
		{
			EnableMenuLogging(true);
			return;
		}
		if (StrStrI(modKeys, "MenuStop"))
		{
			EnableMenuLogging(false);
			return;
		}
		if (StrStrI(modKeys, "MruStart"))
		{
			EnableMruLogging(true);
			return;
		}
		if (StrStrI(modKeys, "MruStop"))
		{
			EnableMruLogging(false);
			return;
		}
		if (StrStrI(modKeys, "ArgTooltipStart"))
		{
			EnableArgTooltipLogging(true);
			return;
		}
		if (StrStrI(modKeys, "ArgTooltipStop"))
		{
			EnableArgTooltipLogging(false);
			return;
		}
		if (StrStrI(modKeys, "DlgStart"))
		{
			EnableDialogLogging(true);
			return;
		}
		if (StrStrI(modKeys, "DlgStop"))
		{
			EnableDialogLogging(false);
			return;
		}
		if (StrStrI(modKeys, "SourceLinksStart"))
		{
			EnableSourceLinksLogging(true);
			return;
		}
		if (StrStrI(modKeys, "SourceLinksStop"))
		{
			EnableSourceLinksLogging(false);
			return;
		}
		if (StrStrI(modKeys, "SourceLinks"))
		{
			if (gVaInteropService)
			{
				WaitThreads(false);
				EdCntPtr ed = g_currentEdCnt;
				if (ed)
				{
					uint cl;
					RunFromMainThread([&ed, &cl]() { cl = (uint)ed->CurLine(); });
					uint startLine = cl, endLine = cl + 1;
					token2 t = modKeys;
					t.read(':');
					t.read(':');
					while (t.more())
					{
						int n = atoi(t.read(":, ").c_str());
						if (n < 0)
							startLine = cl + n;
						else if (n > 0)
							endLine = cl + n + 1;
					}

					RunFromMainThread([startLine, endLine]() // must be synced #VaInteropService_Synced
					                  {
						                  _variant_t vars[2] = {startLine, endLine};
						                  gVaInteropService->UseInvoker(L"SourceLinksAST", L"LogLinks", vars, 2);
					                  });
					return;
				}
			}
			return;
		}
		if (StrStrI(modKeys, "MefTooltipRequests"))
		{
			EnableMefTooltipRequests(true);
			return;
		}
		if (StrStrI(modKeys, "log:URAlt4"))
		{
			EnableSaveOfAlt4Log(true);
			return;
		}
		if (StrStrI(modKeys, "log:URAlt3"))
		{
			EnableSaveOfAlt3Log(true);
			return;
		}
		if (StrStrI(modKeys, "log:URAlt2"))
		{
			EnableSaveOfAlt2Log(true);
			return;
		}
		if (StrStrI(modKeys, "log:URAlt"))
		{
			EnableSaveOfAltLog(true);
			return;
		}
		if (StrStrI(modKeys, "UpdateResults") || StrStrI(modKeys, "log:UR"))
		{
			EnableSaveOfLog(true);
			return;
		}
		if (StrStrI(modKeys, "LBState"))
		{
			WaitForListBox();
			LogListBox(TRUE);
			return;
		}
		if (StrStrI(modKeys, "QuickInfo"))
		{
			WaitThreads(false);
			LogQuickInfo();
			return;
		}
		if (StrStrI(modKeys, "FileNoState"))
		{
			WaitThreads(false);
			if (g_currentEdCnt)
				LogStr(WTString("FileName: " + WTString(Basename(g_currentEdCnt->FileName()))));
			else
				LogStr(WTString("FileName: null ed"));
			return;
		}
		if (StrStrI(modKeys, "Filesize")) // <Log:Filesize>
		{
			WaitThreads(false);

			EdCntPtr ed(g_currentEdCnt);
			WTString tmp(controlTag.Mid(5));
			CStringW file(GetStringArgValue(tmp).Wide());
			if (file.IsEmpty())
			{
				if (ed)
					file = ed->FileName();
				else
					LogStr(WTString("File size: null ed"));
			}
			else if (file.GetLength() > 2 && file[0] != L':')
			{
				if (ed)
				{
					// convert to absolute path
					CStringW curPath(::Path(ed->FileName()));
					file = curPath + L"\\" + file;
				}
				else
				{
					LogStr(WTString("File size: null ed"));
					return;
				}
			}

			if (file.IsEmpty())
				LogStr(WTString("File size: empty file"));
			else if (::IsFile(file))
			{
				const DWORD fs = ::GetFSize(file);
				WTString str;
				str.WTFormat("File size: %lu %ls", fs, (LPCWSTR)::Basename(file));
				LogStr(str);
			}
			else
			{
				WTString str;
				str.WTFormat("File size: %ls does not exist", (LPCWSTR)::Basename(file));
				LogStr(str);
			}
			return;
		}
		if (StrStrI(modKeys, "Scope")) // <Log:Scope>
		{
			WaitThreads(false);
			LogScope();
			return; // Return w/o logging state
		}

		if (StrStrI(modKeys, "Zoom")) // <Log:Zoom>
		{
			WaitThreads(false);
			LogEditorZoom();
			return;
		}

		if (StrStrI(modKeys, "LBInexact")) // <Log:LBInexact>
		{
			WaitForListBox();
			LogState(FALSE);
			return;
		}
		else if (StrStrI(modKeys, "LB")) // <Log:LB>
		{
			WaitForListBox();
			LogState();
			return;
		}
		else if (StrStrI(modKeys, "File")) // <Log:File>
		{
			WaitThreads(false);
			if (g_currentEdCnt)
				LogStr(WTString("FileName: " + WTString(Basename(g_currentEdCnt->FileName()))));
			else
				LogStr(WTString("FileName: null ed"));
		}
		else if (StrStrI(modKeys, "SurroundingLines")) // <Log:SurroundingLines:-1,1>
		{
			WaitThreads(false);
			EdCntPtr ed = g_currentEdCnt;
			if (ed)
			{
				uint cl;
				RunFromMainThread([&ed, &cl]() { cl = (uint)ed->CurLine(); });
				uint startLine = cl, endLine = cl + 1;
				token2 t = modKeys;
				t.read(':');
				t.read(':');
				while (t.more())
				{
					int n = atoi(t.read(":, ").c_str());
					if (n < 0)
						startLine = cl + n;
					else if (n > 0)
						endLine = cl + n + 1;
				}

				WTString text;
				RunFromMainThread([&ed, &text, startLine, endLine]() {
					uint lp, lp2;
					lp = ed->LinePos((int)startLine);
					vCatLog("Editor.Events", "VaEventAT   SurroundingLines pos=%d lines '%d-%d'", lp, startLine, endLine);
					lp2 = ed->LinePos((int)endLine);
					text = ed->GetSubString(lp, lp2);
				});
				LogStr(text);
				return;
			}
		}
		else if (StrStrI(modKeys, "AllLines")) // <Log:AllLines>
		{
			WaitThreads(false);
			EdCntPtr ed = g_currentEdCnt;
			if (ed)
			{
				WTString text;
				RunFromMainThread([&ed, &text]() {
					uint lp, lp2;
					lp = ed->LinePos(0);
					vCatLog("Editor.Events", "VaEventAT   AllLines");
					lp2 = ed->LinePos(99999);
					text = ed->GetSubString(lp, lp2);
				});
				LogStr(text);
				return;
			}
		}
		else if (StrStrI(modKeys, "GUIDEqualityInSelection"))
		{
			WaitThreads();
			LogGUIDEqualityInSelection();
		}
		else if (StrStrI(modKeys, "Selection"))
		{
			WaitThreads();
			LogSelection();
		}
		else if (StrStrI(modKeys, "Log:VSResources:"))
		{
			WaitThreads();
			WTString cat = wtmodKeys.Mid(strlen_i("Log:VSResources:"));
			cat.Trim();
			LogVSResources(cat);
		}
		else if (StrStrI(modKeys, "Tooltips"))
		{
			EnableTooltipLogging(true);
		}
		else if (!wtmodKeys.CompareNoCase("<log"))
		{
			WaitThreads();
			LogState();
			return;
		}
		else
			WaitThreads();

		if (mImplicitHelpers)
			LogState();

		if (StrStrI(modKeys, "HCB")) // <Log:HCB>
			LogHcb();
		else if (StrStrI(modKeys, "RefResults")) // <Log:RefResults>
			LogReferencesResults();
		else if (StrStrI(modKeys, "BraceMarkers")) // <Log:BraceMarkers>
			LogBraceMarkers();
		else if (StrStrI(modKeys, "Markers")) // <Log:Markers>
			LogMarkers();
		else if (StrStrI(modKeys, "Minihelp")) // <Log:Minihelp>
			LogMinihelp();
		else if (StrStrI(modKeys, "ParamInfo")) // <Log:ParamInfo>
			LogParamInfo();
		else if (StrStrI(modKeys, "VaOutline")) // <Log:VaOutline>
			LogVaOutline();
	}
	else if (StrStrI(modKeys, "<sync")) // <Sync>
	{
		AstSyncVa();
	}
	else if (StrStrI(modKeys, "<CheckNoDlg:"))
	{
		// <CheckNoDlg:Some Dialog Caption>
		CheckNoDialog(wtmodKeys.Mid(12).c_str());
	}
	else if (StrStrI(modKeys, "<CheckExceptionDlg"))
	{
		CheckExceptionDlg(2000);
	}
	else if (StrStrI(modKeys, "<paste:"))
	{
		// <paste:Some text>
		HWND hFoc = ::VAGetFocus();
		::SaveToClipboard(hFoc, wtmodKeys.Mid(7).Wide());
		Typomatic::TypeString("<CTRL+V>", FALSE);
	}
	else if (StrStrI(modKeys, "<RunLogCloseFindRefs")) // <RunLogCloseFindRefs>
	{
		Typomatic::TypeString("<CMD-NoWait:VAssistX.FindReferences>", FALSE);
		Typomatic::TypeString("<wait:100>", FALSE);
		Typomatic::TypeString("<LOG:RefResults>", FALSE);
		Typomatic::TypeString("<wait:100>", FALSE);
		Typomatic::TypeString("<CloseFindRefs>", FALSE);
	}
	else if (StrStrI(modKeys, "<SaveTmpLogSize")) // <SaveTmpLogSize>
	{
		ProcessControlCharacters("<DeleteFile:vaTmp.va");
		ProcessControlCharacters("<SaveAs:vaTmp.va");
		ProcessControlCharacters("<cmd:File.Close");
		ProcessControlCharacters("<log:Filesize:vaTmp.va");
	}
	else if (StrStrI(modKeys, "<SaveAs")) // <SaveAs:xxx>
	{
		WTString file(GetStringArgValue(controlTag));
		if (file.IsEmpty())
		{
			LogStr(WTString("File save as: no file specified"));
			return;
		}

		EdCntPtr ed(g_currentEdCnt);
		if (!ed)
		{
			LogStr(WTString("File save as: error, no active editor"));
			return;
		}

		ProcessControlCharacters("<cmd-x:File.SaveSelectedItemsAs");
		ProcessControlCharacters("<WaitDlg:Save File As");
		ProcessControlCharacters("<typingDelay:250");
		Typomatic::TypeString(file.c_str(), FALSE);
		Typomatic::TypeString("<enter>", FALSE);
		for (int idx = 0; idx < 5; ++idx)
		{
			::Sleep(2000);
			if (::IsFile(Path(ed->FileName()) + L"\\" + CStringW(file.Wide())))
				break;
		}
		ProcessControlCharacters("<2:esc");
	}
	else if (StrStrI(modKeys, "<CloseFindRefs")) // <CloseFindRefs>
	{
		Typomatic::TypeString("<CMD-NoWait:VAssistX.FindReferencesResults>", FALSE);
		Typomatic::TypeString("<wait:100>", FALSE);
		Typomatic::TypeString("<CMD-NoWait:Window.CloseToolWindow>", FALSE);
		Typomatic::TypeString("<Wait:50>", FALSE);
		Typomatic::TypeString("<CMD-NoWait:Window.ActivateDocumentWindow>", FALSE);
	}
	else if (StrStrI(modKeys, "<CloseVaView")) // <CloseVaView>
	{
		Typomatic::TypeString("<CMD-NoWait:VAssistX.VAView>", FALSE);
		Typomatic::TypeString("<wait:100>", FALSE);
		Typomatic::TypeString("<CMD-NoWait:Window.CloseToolWindow>", FALSE);
		Typomatic::TypeString("<Wait:50>", FALSE);
		Typomatic::TypeString("<CMD-NoWait:Window.ActivateDocumentWindow>", FALSE);
		if (gShellAttr->IsDevenv12OrHigher())
		{
			// due to vs2013 preview 1 connect id 790018
			Typomatic::TypeString("<alt+w>1", FALSE);
		}
	}
	else if (StrStrI(modKeys, "<ShowVaParamInfo"))
	{
		WaitThreads(FALSE);
		RunFromMainThread<void>(ShowVaParamInfo);
	}
	else if (StrStrI(modKeys, "<WAIT"))
	{
		if (StrStrI(modKeys, "Threads"))
			WaitThreads();
		else if (StrStrI(modKeys, "Menu"))
		{
			Sleep(100);
			int n = 20;
			for (; n && (!RefactoringActive::IsActive() && !PopupMenuXP::IsMenuActive() && !gExternalMenuCount); n--)
				Sleep(100);

			if (!StrStrI(modKeys, "MenuNoThrow"))
			{
				if (!n)
					throw WtException("Wait for menu timed-out");
			}
		}
		else if (StrStrI(modKeys, "LB"))
			WaitForListBox();
		else if (StrStrI(modKeys, "VSPopup"))
			WaitForVSPopup(FALSE);
		else if (StrStrI(modKeys, "<waitdlg:"))
		{
			// <WaitDlg:Some Dialog Caption>
			WaitDlg(wtmodKeys.Mid(9).c_str(), TRUE);
		}
		else if (StrStrI(modKeys, "<waitdlgnothrow:"))
		{
			// <WaitDlgNoThrow:Some Dialog Caption>
			WaitDlg(wtmodKeys.Mid(16).c_str(), FALSE);
		}
		else if (StrStrI(modKeys, "<waitdialog:"))
		{
			// <WaitDialog:Some Dialog Caption>
			WaitDlg(wtmodKeys.Mid(12).c_str(), TRUE);
		}
		else if (StrStrI(modKeys, "<wait:"))
			Sleep((DWORD)atoi(modKeys + strlen("<WAIT:")));
		else
		{
			WTString msg;
			msg.WTFormat("ERROR: unknown wait command '%s'", modKeys);
			throw WtException(msg);
		}
	}
	else if (StrStrI(modKeys, "<ACCEPT"))
	{
		// Wait for listbox, then press <tab> to select
		if (WaitForListBox())
		{
			SimulateKeyPress(VK_TAB, TRUE);
			// IVs.OnCommit() processes messages, so WaitThreads may return before
			// the actual commit and before our listbox is dismissed.
			// As a result, <Accept><Log:LB> will log the contents as it is being dismissed(Colin crash).
			Sleep(300); // To let VA process <TAB>.
		}
		else
			LogStr("No listbox for <ACCEPT> command");
	}
	else if (StrStrI(modKeys, "<TYPINGDELAY:"))
	{
		// Adjust time delay between keys
		m_TypingDelay = (DWORD)atoi(modKeys + strlen("<TYPINGDELAY:"));
	}
	else if (StrStrI(modKeys, "<goto:"))
	{
		// goto line number in cur ed
		uint ln = (uint)atoi(modKeys + strlen("<goto:"));
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			RunFromMainThread([&ed, ln]() {
				uint lp = ed->LinePos((int)ln);
				ed->SetPos(lp);
			});
		}
	}
	else if (StrStrI(modKeys, "<gotochar:"))
	{
		// goto char at specified position on current line
		uint ch = (uint)atoi(modKeys + strlen("<gotochar:"));
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			RunFromMainThread([&ed, ch]() {
				uint cp = ed->CurPos();
				uint ln = (uint)ed->LineFromChar((long)cp);
				uint lp = (uint)ed->LineIndex((long)ln) - 1;
				ed->SetPos(lp + ch);
			});
		}
	}
	else if (StrStrI(modKeys, "<GotoBehavior:"))
	{
		uint gb = (uint)atoi(modKeys + strlen("<GotoBehavior:"));
		Psettings->mGotoInterfaceBehavior = gb;
	}
	else if (StrStrI(modKeys, "<DisableLogLinebreakNormalization"))
	{
		EnableLogLinebreakNormalization(false);
	}
	else if (StrStrI(modKeys, "<ClassMemberNamingBehavior:"))
	{
		uint nb = (uint)atoi(modKeys + strlen("<ClassMemberNamingBehavior:"));
		if (nb > Settings::cmn_last)
			nb = Settings::cmn_prefixDependent;
		Psettings->mClassMemberNamingBehavior = nb;
	}
	else if (StrStrI(modKeys, "<ForceCaseInsensitiveFilters:"))
	{
		int csf = atoi(modKeys + strlen("<ForceCaseInsensitiveFilters:"));
		Psettings->mForceCaseInsensitiveFilters = !!csf;
	}
	else if (StrStrI(modKeys, "<EnableCodeInspections:"))
	{
		int eci = atoi(modKeys + strlen("<EnableCodeInspections:"));
		_ASSERTE(!eci || (gShellAttr && gShellAttr->IsDevenv14OrHigher())); // do not enable code inspection unless in
		                                                                    // vs2015+ due to memory exhaustion
		if (gVaInteropService)
		{
			WaitThreads(false);
			RunFromMainThread([eci]() // must be synced #VaInteropService_Synced
			                  {
				                  _variant_t var(eci != 0);
				                  gVaInteropService->UseInvoker(L"CI_AST", L"EnableCI", &var, 1);
			                  });
		}
	}
	else if (const char* cmd2 = "<CISettings:"; StrStrI(modKeys, cmd2))
	{
		if (gVaInteropService)
		{
			const std::string operand(modKeys + strlen(cmd2));

			WaitThreads(false);
			RunFromMainThread([operand, wtmodKeys, this]() // must be synced #VaInteropService_Synced
			                  {
				                  if (operand == "revert")
					                  gVaInteropService->UseInvoker(L"CI_AST", L"RevertSettings");
				                  else if (auto colon = operand.find(':'); colon != std::string::npos)
				                  {
					                  std::string name = operand.substr(0, colon);
					                  std::string value = operand.substr(colon + 1);
					                  _ASSERTE(!name.empty() && !value.empty());

					                  if (!name.empty() && !value.empty())
					                  {
						                  _variant_t args[] = {name.c_str(), value.c_str()};
						                  gVaInteropService->UseInvoker(L"CI_AST", L"SetSetting", args, _countof(args));
					                  }
					                  else
						                  LogStr("Invalid command format: " + wtmodKeys);
				                  }
				                  else
					                  LogStr("Invalid command: " + wtmodKeys);
			                  });
		}
	}
	else if (StrStrI(modKeys, "<CMD:Edit.Copy"))
	{
		// Don't invalidate buffer
		const WTString cmd(wtmodKeys.Mid(5));
		RunVSCommand(cmd, FALSE);
	}
	else if (StrStrI(modKeys, "<CMD:"))
	{
		// IDE command <Edit.Left>
		WTString cmd(wtmodKeys.Mid(5));
		int repeatCount = atoi(cmd.c_str());
		if (repeatCount)
			cmd = cmd.Mid(cmd.Find(':') + 1); // <CMD:10:Edit.Left>
		else
			repeatCount = 1;

		for (int cnt = 0; cnt < repeatCount; ++cnt)
		{
			RunVSCommand(cmd);

			if (cnt && !IsOK())
				return;
		}

		if (cmd == "VAssistX.OpenCorrespondingFile")
			AstSyncVa();
	}
	else if (StrStrI(modKeys, "<CMD-NoWait:"))
	{
		// IDE command <Edit.Left> without intermediate call to WaitThreads
		WTString cmd(wtmodKeys.Mid(12).c_str());
		int repeatCount = atoi(cmd.c_str());
		if (repeatCount)
			cmd = cmd.Mid(cmd.Find(':') + 1); // <CMD-NoWait:10:Edit.Left>
		else
			repeatCount = 1;

		for (int cnt = 0; cnt < repeatCount; ++cnt)
		{
			RunVSCommand(cmd, TRUE, FALSE);

			if (cnt && !IsOK())
				return;
		}
	}
	else if (StrStrI(modKeys, "<CMD-x:"))
	{
		// raw IDE command <Edit.Left> without any helpful stuff that gets in the way
		WTString cmd(wtmodKeys.Mid(7).c_str());
		int repeatCount = atoi(cmd.c_str());
		if (repeatCount)
			cmd = cmd.Mid(cmd.Find(':') + 1); // <CMD-x:10:Edit.Left>
		else
			repeatCount = 1;

		for (int cnt = 0; cnt < repeatCount; ++cnt)
		{
			RunVSCommand(cmd, FALSE, FALSE, TRUE);

			if (cnt && !IsOK())
				return;
		}
	}
	else if (StrStrI(modKeys, "<Find:"))
	{
		// IDE Find <Find:substring>
		WTString pattern = TokenGetField(wtmodKeys.c_str() + 6, ">");
		if (!IdeFind(pattern.c_str()))
		{
			ConsoleTrace("ERROR: Find failed\r\n");
			// consider throwing an exception - but first confirm that
			// Find is never used for negative results (looking for
			// something that shouldn't exist)
			//			throw WtException("ERROR: Find failed");
		}
	}
	else if (StrStrI(modKeys, "<FindRegExp:"))
	{
		// IDE Find <FindRegExp:regexp>
		WTString pattern = TokenGetField(wtmodKeys.c_str() + 12, ">");
		if (!IdeFind(pattern.c_str(), TRUE))
		{
			ConsoleTrace("ERROR: FindRegExp failed\r\n");
			// consider throwing an exception - but first confirm that
			// Find is never used for negative results (looking for
			// something that shouldn't exist)
			//			throw WtException("ERROR: FindRegExp failed");
		}
	}
	else if (StrStrI(modKeys, "<SnippetDir:"))
	{
		// Loads snippets from a local subdir <SnippetDir:subdir>
		EdCntPtr ed = g_currentEdCnt;
		_ASSERTE(ed);
		if (ed)
		{
			gTestSnippetSubDir = Path(ed->FileName()) + L"\\" + TokenGetField(wtmodKeys.c_str() + 12, ">").Wide();
			gAutotextMgr->Load(NULL);           // To force loading of new snippets
			gAutotextMgr->Load(gTypingDevLang); // Load new snippets
		}
	}
	else if (StrStrI(modKeys, "<QueryStatus:Refactor:"))
	{
		WTString cmd(wtmodKeys.Mid(22));
		DWORD cmdId = 0;
		if (cmd == "Rename")
			cmdId = icmdVaCmd_RefactorRename;
		else if (cmd == "ChangeSignature")
			cmdId = icmdVaCmd_RefactorChangeSignature;
		else if (cmd == "ImplementVirtualMethods" || cmd == "ImplementInterface")
			cmdId = icmdVaCmd_RefactorImplementInterface;
		else if (cmd == "ExtractMethod")
			cmdId = icmdVaCmd_RefactorExtractMethod;
		else if (cmd == "MoveImplementation")
			cmdId = icmdVaCmd_RefactorMoveImplementation;
		else if (cmd == "CreateImplementation")
			cmdId = icmdVaCmd_RefactorCreateImplementation;
		else if (cmd == "CreateDeclaration")
			cmdId = icmdVaCmd_RefactorCreateDeclaration;
		else if (cmd == "AddMember")
			cmdId = icmdVaCmd_RefactorAddMember;
		else if (cmd == "AddSimilarMember")
			cmdId = icmdVaCmd_RefactorAddSimilarMember;
		else if (cmd == "CreateFromUsage")
			cmdId = icmdVaCmd_RefactorCreateFromUsage;
		else if (cmd == "CreateFile")
			cmdId = icmdVaCmd_RefactorCreateFile;
		else if (cmd == "RenameFiles")
			cmdId = icmdVaCmd_RefactorRenameFiles;
		else if (cmd == "DocumentMethod")
			cmdId = icmdVaCmd_RefactorDocumentMethod;
		else if (cmd == "EncapsulateField")
			cmdId = icmdVaCmd_RefactorEncapsulateField;
		else if (cmd == "MoveSelectionToNewFile")
			cmdId = icmdVaCmd_RefactorMoveSelToNewFile;
		else if (cmd == "IntroduceVariable")
			cmdId = icmdVaCmd_RefactorIntroduceVariable;
		else if (cmd == "BracesToggle")
			cmdId = icmdVaCmd_RefactorAddRemoveBraces;
		else if (cmd == "BracesAdd")
			cmdId = icmdVaCmd_RefactorAddBraces;
		else if (cmd == "BracesRemove")
			cmdId = icmdVaCmd_RefactorRemoveBraces;
		else if (cmd == "CreateMissingCases")
			cmdId = icmdVaCmd_RefactorCreateMissingCases;
		else if (cmd == "MoveImplementationToHeader")
			cmdId = icmdVaCmd_RefactorMoveImplementationToHdr;
		else if (cmd == "ConvertBetweenPointerAndInstance")
			cmdId = icmdVaCmd_RefactorConvertBetweenPointerAndInstance;
		else if (cmd == "SimplifyInstanceDeclaration")
			cmdId = icmdVaCmd_RefactorSimplifyInstanceDeclaration;
		else if (cmd == "AddForwardDeclaration")
			cmdId = icmdVaCmd_RefactorAddForwardDeclaration;
		else if (cmd == "ConvertUnscopedEnumToScopedEnum")
			cmdId = icmdVaCmd_RefactorConvertEnum;
		else if (cmd == "MoveClassToNewFile")
			cmdId = icmdVaCmd_RefactorMoveClassToNewFile;
		else if (cmd == "SortClassMethods")
			cmdId = icmdVaCmd_RefactorSortClassMethods;

		QueryStatusAndLogResult(IVaService::ct_refactor, cmdId, cmd);
	}
	else if (StrStrI(modKeys, "<QueryStatus:Editor:"))
	{
		WTString cmd(wtmodKeys.Mid(20));
		DWORD cmdId = 0;
		if (cmd == "GotoRelated")
			cmdId = icmdVaCmd_SuperGoto;
		else if (cmd == "GotoMember")
			cmdId = icmdVaCmd_GotoMember;

		QueryStatusAndLogResult(IVaService::ct_editor, cmdId, cmd);
	}
	else if (StrStrI(modKeys, "<QueryStatus:Global:"))
	{
		WTString cmd(wtmodKeys.Mid(20));
		DWORD cmdId = 0;
		// 		if (cmd == "")
		// 			cmdId = ;

		QueryStatusAndLogResult(IVaService::ct_global, cmdId, cmd);
	}
	else if (StrStrI(modKeys, "<QueryStatus:Outline:"))
	{
		WTString cmd(wtmodKeys.Mid(21));
		DWORD cmdId = 0;
		// 		if (cmd == "")
		// 			cmdId = ;

		QueryStatusAndLogResult(IVaService::ct_outline, cmdId, cmd);
	}
	else if (StrStrI(modKeys, "<QueryStatus:VaView:"))
	{
		WTString cmd(wtmodKeys.Mid(20));
		DWORD cmdId = 0;
		// 		if (cmd == "")
		// 			cmdId = ;

		QueryStatusAndLogResult(IVaService::ct_vaview, cmdId, cmd);
	}
	else if (StrStrI(modKeys, "<QueryStatus:FindRefResults:"))
	{
		WTString cmd(wtmodKeys.Mid(28));
		DWORD cmdId = 0;
		// 		if (cmd == "")
		// 			cmdId = ;

		QueryStatusAndLogResult(IVaService::ct_findRefResults, cmdId, cmd);
	}
	else if (StrStrI(modKeys, "<CompareFiles:"))
	{
		/////////////////////////////////////////////////////////
		// get paths in form: path\of\file1.exp;path\of\file2.exp

		CStringW files(GetStringArgValue(controlTag).Wide());

		if (files.IsEmpty())
		{
			ConsoleTrace("ERROR: Empty arguments in CompareFiles\r\n");
			return;
		}

		int pos = 0;
		CStringW file1 = files.Tokenize(L";", pos);
		CStringW file2 = files.Tokenize(L";", pos);

		if (file1.IsEmpty() || file2.IsEmpty())
		{
			ConsoleTrace("ERROR: Empty arguments in CompareFiles\r\n");
			return;
		}

		CStringW msg;
		CString__FormatW(msg, L"Comparing files: %s", (LPCWSTR)files);
		LogStrW(msg);

		/////////////////////////////////////////////////////////
		// replace %astlogs% with "..\AutoTestLogs\vsXXResults"

		CStringW filePath = ::Path(mFile);
		CStringW logDir = L"AutoTestLogs\\" + gShellAttr->GetDbSubDir() + L"Results";
		CStringW logPath = ::BuildPath(logDir, filePath, true, false);

		if (!::IsPathAbsolute(file1))
		{
			file1.Replace(L"%astlogs%", logPath);
			file1.Replace(L"%ASTLOGS%", logPath);
		}

		if (!::IsPathAbsolute(file2))
		{
			file2.Replace(L"%astlogs%", logPath);
			file2.Replace(L"%ASTLOGS%", logPath);
		}

		LogFilesComparison(file1, file2);
	}
	else if (StrStrI(modKeys, "<CompareLogs:"))
	{
		/////////////////////////////////////////////////////////
		// get test names in form leftTest;rightTest

		CStringW args(GetStringArgValue(controlTag).Wide());

		if (args.IsEmpty())
		{
			ConsoleTrace("ERROR: Empty arguments in CompareLogs\r\n");
			return;
		}

		int pos = 0;
		CStringW name1 = args.Tokenize(L";", pos);
		CStringW name2 = args.Tokenize(L";", pos);

		if (name1.IsEmpty() || name2.IsEmpty())
		{
			ConsoleTrace("ERROR: Empty arguments in CompareLogs\r\n");
			return;
		}

		CStringW msg;
		CString__FormatW(msg, L"Comparing logs: %s", (LPCWSTR)args);
		LogStrW(msg);

		CStringW base = ::Basename(mFile);
		name1 = base + L"_" + name1;
		name2 = base + L"_" + name2;

		CStringW path1 = GetLogPath(mFile, name1);
		CStringW path2 = GetLogPath(mFile, name2);

		LogFilesComparison(path1, path2);
	}
	else if (StrStrI(modKeys, "<SmartSelectLogging:"))
	{
		CStringA args(GetStringArgValue(controlTag).c_str());

		if (args.IsEmpty())
		{
			ConsoleTrace("ERROR: Empty arguments in SmartSelectLogging\r\n");
			return;
		}

		DWORD mask = sslog_none;

		// apply each token

		int pos = 0;
		const LPCSTR delim = " |,;:";
		CStringA tok = args.Tokenize(delim, pos);
		while (!tok.IsEmpty())
		{
			if (tok.CompareNoCase("default") == 0)
				mask |= sslog_default;
			else if (tok.CompareNoCase("peek") == 0)
				mask |= sslog_peek;
			else if (tok.CompareNoCase("block") == 0)
				mask |= sslog_block;

			tok = args.Tokenize(delim, pos);
		}

		SetSmartSelectLogMask(mask);
	}
	else if (StrStrI(modKeys, "<ModifyExprSetFlags:"))
	{
		DWORD flags = CSettings::ModExpr_None;
		CStringA args(GetStringArgValue(controlTag).c_str());

		int pos = 0;
		const LPCSTR delim = " ,;:";
		CStringA tok = args.Tokenize(delim, pos);
		while (!tok.IsEmpty())
		{
			bool add = true;

			if (!tok.IsEmpty() && tok[0] == '~')
			{
				tok.Delete(0);
				add = false;
			}

			// DEFAULT, NONE, NODLG, INVERT, LOGIC, RELAT, REMNOT, PARENS, DIFF

			if (0 == tok.CompareNoCase("DEFAULT"))
				flags = add ? (flags | CSettings::ModExpr_Default) : (flags & ~CSettings::ModExpr_Default);
			else if (0 == tok.CompareNoCase("INVERT"))
				flags = add ? (flags | CSettings::ModExpr_Invert) : (flags & ~CSettings::ModExpr_Invert);
			else if (0 == tok.CompareNoCase("LOGIC"))
				flags = add ? (flags | CSettings::ModExpr_ToggleLogOps) : (flags & ~CSettings::ModExpr_ToggleLogOps);
			else if (0 == tok.CompareNoCase("RELAT"))
				flags = add ? (flags | tagSettings::ModExpr_ToggleRelOps) : (flags & ~CSettings::ModExpr_ToggleRelOps);
			else if (0 == tok.CompareNoCase("REMNOT"))
				flags = add ? (flags | CSettings::ModExpr_NotOpRemoval) : (flags & ~CSettings::ModExpr_NotOpRemoval);
			else if (0 == tok.CompareNoCase("PARENS"))
				flags = add ? (flags | CSettings::ModExpr_ParensRemoval) : (flags & ~CSettings::ModExpr_ParensRemoval);
			else if (0 == tok.CompareNoCase("DIFF"))
				flags = add ? (flags | tagSettings::ModExpr_ShowDiff) : (flags & ~CSettings::ModExpr_ShowDiff);
			else if (0 == tok.CompareNoCase("NODLG"))
				flags = add ? (flags | CSettings::ModExpr_NoDlg) : (flags & ~CSettings::ModExpr_NoDlg);

			tok = args.Tokenize(delim, pos);
		}

		Psettings->mModifyExprFlags = flags;
	}
	else if (StrStrI(modKeys, "<SourceLinksExec:") || StrStrI(modKeys, "<SLExec:") || StrStrI(modKeys, "<CIExec:"))
	{
		LPCWSTR invoker = nullptr;

		if (StrStrI(modKeys, "<SourceLinksExec:") || StrStrI(modKeys, "<SLExec:"))
			invoker = L"SourceLinksAST";
		else if (StrStrI(modKeys, "<CIExec:"))
			invoker = L"CI_AST";

		if (gVaInteropService && invoker)
		{
			WaitThreads(false);
			auto tokens = GetStringArgValues(controlTag, ';');
			if (!tokens.empty())
			{
				CStringW name = tokens[0].Wide();
				LPCWSTR namePtr = name;

				if (tokens.size() == 1)
				{
					RunFromMainThread([namePtr, invoker]() // must be synced #VaInteropService_Synced
					                  { gVaInteropService->UseInvoker(invoker, namePtr); });
				}
				else
				{
					std::vector<variant_t> args;
					args.resize(tokens.size() - 1);

					for (size_t x = 1; x < tokens.size(); x++)
						args[x - 1] = (LPCWSTR)tokens[x].Wide();

					VARIANT* argsPtr = &args.front();
					int argsCount = (int)args.size();

					RunFromMainThread(
					    [namePtr, argsPtr, argsCount, invoker]() // must be synced #VaInteropService_Synced
					    { gVaInteropService->UseInvoker(invoker, namePtr, argsPtr, argsCount); });
				}
			}
		}
		return;
	}
	else if (StrStrI(modKeys, "<VaOptionsDlg:") || StrStrI(modKeys, "<VaOptionsDialog:"))
	{
		WaitThreads(false);
		gTestVaOptionsPage.SetString(GetStringArgValue(controlTag).c_str());
		gTestVaOptionsPage.Trim();
		RunVSCommand("VAssistX.Options");
		WaitDlg("Visual Assist Options", TRUE);
	}
	else
		__super::ProcessControlCharacters(controlTag);
}

BOOL VAAutomationControl::WaitForListBox()
{
	WaitThreads();
	if (!Psettings->m_enableVA)
	{
		Sleep(1000);
		return TRUE;
	}
	for (int i = 0; i < 100 && !g_CompletionSet->HasSelection(); i++)
		Sleep(20);
	return g_CompletionSet->HasSelection();
}

BOOL VAAutomationControl::WaitForVSPopup(BOOL ignoreToolTips)
{
	WaitThreads();
	if (!Psettings->m_enableVA)
	{
		Sleep(1000);
		return TRUE;
	}
	Sleep(200);
	BOOL hasPopup = false;
	RunFromMainThread([&ignoreToolTips, &hasPopup]() {
		for (int i = 0; i < 100 && !HasVsNetPopup(ignoreToolTips); i++)
			Sleep(20);
		hasPopup = HasVsNetPopup(ignoreToolTips);
	});
	return hasPopup;
}

void VAAutomationControl::WaitDlg(LPCSTR caption, BOOL throwIfNotFound /*= TRUE*/)
{
	Sleep(100);
	WTString caption1(caption), caption2;

	if (caption1 == "Convert Between Pointer and Instance")
	{
		caption1 = "Convert Instance to Pointer";
		caption2 = "Convert Pointer to Instance";
	}

	const int kSleepAmt = 250;
	for (int duration = 0; duration < 10000; duration += kSleepAmt)
	{
		HWND wnd = GetForegroundWindow();
		// can a window be hidden if it is the ForegroundWindow?
		if (!wnd || !IsWindowVisible(wnd))
		{
			if (caption1 != "Change Signature" && caption1 != "Rename" && caption1 != "Encapsulate Field" &&
			    -1 != caption1.Find("Open File in Solution"))
			{
				// this branch is an attempt to fix CloseAllDocuments on v16u6-w10-ue,
				// but no repro original problem so unable to test the branch.
				// Microsoft Edge often appears behind VS with focus? AST typed code into Edge url bar.
				ConsoleTrace("WaitDlg: no wnd\r\n");

				GUITHREADINFO inf;
				ZeroMemory(&inf, sizeof(GUITHREADINFO));
				inf.cbSize = sizeof(GUITHREADINFO);
				GetGUIThreadInfo(g_mainThread, &inf);
				WTString wndText;
				if (inf.hwndActive)
				{
					wndText = GetWindowTextString(inf.hwndActive);
					if (wndText == caption1)
					{
						ConsoleTrace("WaitDlg: no wnd, 1\r\n");
						return;
					}
				}

				if (inf.hwndCapture)
				{
					wndText = GetWindowTextString(inf.hwndCapture);
					if (wndText == caption1)
					{
						ConsoleTrace("WaitDlg: no wnd, 2\r\n");
						return;
					}
				}

				if (inf.hwndFocus)
				{
					wndText = GetWindowTextString(inf.hwndFocus);
					if (wndText == caption1)
					{
						ConsoleTrace("WaitDlg: no wnd, 3\r\n");
						return;
					}
				}
			}
			else
				ConsoleTrace("WaitDlg: no wnd ignored\r\n");
		}
		else
		{
			WTString wndText = GetWindowTextString(wnd);
			if (0 == wndText.Find("VA Snippet: "))
			{
				// [case: 78555]
				// new snippet editor uses different runtime prompt caption (adds prefix)
				wndText = wndText.Mid(12);
			}

			// consider using a regexp?
			if (wndText == caption1 || (!caption2.IsEmpty() && wndText == caption2))
			{
				GUITHREADINFO inf;
				ZeroMemory(&inf, sizeof(GUITHREADINFO));
				inf.cbSize = sizeof(GUITHREADINFO);
				GetGUIThreadInfo(g_mainThread, &inf);
				if (inf.hwndActive == wnd)
				{
					if (wndText == "Change Signature" || wndText == "Rename" || wndText == "Encapsulate Field" ||
					    0 == wndText.Find("Open File in Solution"))
					{
						// [case: 83648]
						// make sure the cancel button is active/enabled before returning.
						// do not return until Stop changes to Cancel and is enabled.
						for (duration = 0; duration < 120000; duration += kSleepAmt)
						{
							HWND h = GetDlgItem(wnd, IDCANCEL);
							if (h && IsWindowEnabled(h))
							{
								wndText = GetWindowTextString(h);
								if (wndText == "&Cancel" || wndText == "Cancel")
									break;
							}
							Sleep(kSleepAmt);
						}
					}
					// 					else if (wndText == "Visual Assist Snippet Editor Error")
					// 					{
					// 						// [case: 101883]
					// 						// attempt to dismiss the error dialog by pressing No
					// 						// fails due to incorrect foregroundwindow?
					// 						Typomatic::TypeString("<alt+n>");
					// 					}
					return;
				}
			}
		}

		Sleep(kSleepAmt);
	}

	if (!throwIfNotFound)
		return;

	// failed
	// Press escape to clear any remaining dialog, using Typomatic:: to preserve logging.
	CheckExceptionDlg();
	Typomatic::TypeString("<esc>");
	Typomatic::TypeString("<esc>");

	WTString msg;
	msg.WTFormat("%s failed to find dialog with caption: %s", WTString(m_testName).c_str(), caption);
	throw WtException(msg);
}

void VAAutomationControl::CheckNoDialog(LPCSTR caption)
{
	Sleep(100);

	const int kSleepAmt = 250;
	for (int duration = 0; duration < 4000; duration += kSleepAmt)
	{
		HWND wnd = GetForegroundWindow();
		// can a window be hidden if it is the ForegroundWindow?
		if (wnd && IsWindowVisible(wnd))
		{
			const WTString wndText(GetWindowTextString(wnd));
			// TODO: use regexp?
			if (wndText == caption)
			{
				GUITHREADINFO inf;
				ZeroMemory(&inf, sizeof(GUITHREADINFO));
				inf.cbSize = sizeof(GUITHREADINFO);
				GetGUIThreadInfo(g_mainThread, &inf);
				if (inf.hwndActive == wnd)
				{
					// failed
					// Press escape to clear dialog, using Typomatic:: to preserve logging.
					Typomatic::TypeString("<esc>");
					Typomatic::TypeString("<esc>");

					WTString msg;
					msg.WTFormat("%s found unexpected dialog with caption: %s", WTString(m_testName).c_str(), caption);
					throw WtException(msg);
				}
			}
		}

		Sleep(kSleepAmt);
	}
}

void VAAutomationControl::QueryStatusAndLogResult(IVaService::CommandTargetType tt, DWORD cmdId, const WTString& cmd)
{
	WTString msg;
	if (cmdId)
	{
		DWORD status = gVaService->QueryStatus(tt, cmdId);
		if (IVaService::ct_refactor == tt && icmdVaCmd_RefactorRenameFiles == cmdId)
		{
			// [case: 111679]
			RefactorFlag flg = VARef_RenameFilesFromRefactorTip;
			status = (DWORD)::CanRefactor(flg);
			if (!status && !::DisplayDisabledRefactorCommand(flg))
				status = UINT_MAX;
		}
		msg.WTFormat("QueryStatus %s -> %lx", cmd.c_str(), status);
	}
	else
		msg.WTFormat("QueryStatus fail - unhandled command (%s)", cmd.c_str());

	LogStr(msg);
}

void VAAutomationLogging::LogParamInfo()
{
	EdCntPtr ed = g_currentEdCnt;
	if (ed)
	{
		Sleep(500); // allow for arginfo timers to fire. (ID_ARGTEMPLATE_DISPLAY_TIMER is 500ms)
		RunFromMainThread([&ed, this]() {
			if (ed->m_ttParamInfo->GetSafeHwnd() && ed->m_ttParamInfo->IsWindowVisible())
			{
				LogStr("Our ParamInfo");
				LogStr(ed->m_ttParamInfo->ToString());
			}
			else if (HasVsNetPopup(FALSE))
			{
				// not necessarily param info, but any vs tooltip or listbox.
				// log string should match that of LogQuickInfo, but test
				// results would need to be updated
				LogStr("Their ParamInfo");
			}
			else
				LogStr("No ParamInfo");
		});
	}
}

void VAAutomationLogging::LogQuickInfo()
{
	EdCntPtr ed = g_currentEdCnt;
	if (ed)
	{
		Sleep(200); // allow for mouse move timer to fire
		RunFromMainThread([&ed, this]() {
			if (ed->m_ttTypeInfo && ed->m_ttTypeInfo->GetSafeHwnd() && ed->m_ttTypeInfo->IsWindowVisible())
			{
				LogStr("VA QuickInfo");
				LogStr(ed->m_ttTypeInfo->ToString());
			}
			else if (HasVsNetPopup(FALSE))
			{
				// not necessarily quick info, but any vs tooltip or listbox
				LogStr("VS Popup (Listbox, ParamInfo or QuickInfo)");
			}
			else
				LogStr("No QuickInfo");
		});
	}
}

void VAAutomationLogging::LogState(BOOL exactListboxContents /*= TRUE*/)
{
	try
	{
		// NOTE: This block really isn't thread safe...  Hence the try/catch
		// VA should be idle at this state, but we should change to TODO if we get exceptions.
		WaitThreads();
		EdCntPtr ed = g_currentEdCnt;
		if (ed)
		{
			// TODO: Needs better surrounding text, m_testStartPos could be from another file.
			LogStr(WTString("Surrounding Text:"));
			RunFromMainThread([&ed, this]() {
				if (ed->FileName() != mFile)
				{
					// Log line above and below the new file.
					int curLine = ed->CurLine();
					uint lp1, lp2;
					lp1 = ed->LinePos(curLine ? curLine - 1 : 0);
					lp2 = ed->LinePos(curLine + 2);
					LogStr(ed->GetSubString(lp1, lp2));
				}
				else
				{
					uint cp = ed->CurPos();
					LogStr(ed->GetSubString(m_testStartPos, cp));
				}
			});
		}

		if (g_CompletionSet && g_CompletionSet->IsExpUp(ed))
			LogListBox(FALSE, exactListboxContents);
	}
	catch (...)
	{
		LogStr(WTString("Exception logging state."));
	}
}

void VAAutomationLogging::LogHcb()
{
	try
	{
		LogStr(WTString("HCB info:"));
		if (g_CVAClassView)
			LogStr(g_CVAClassView->GetLastInfoString());
		else
			LogStr(WTString("null VaClassView"));
	}
	catch (...)
	{
		LogStr(WTString("Exception logging HCB info."));
	}
}

void VAAutomationLogging::LogReferencesResults()
{
	try
	{
		FindReferencesPtr refs(g_References);
		if (refs)
			LogStrW(refs->GetSummary());
		else
			LogStr(WTString("null g_References"));
	}
	catch (...)
	{
		LogStr(WTString("Exception logging Find References results."));
	}
}

void VAAutomationLogging::LogVaOutline()
{
	try
	{
		if (gVaService && gVaService->GetOutlineFrame())
			LogStrW(gVaService->GetOutlineFrame()->GetOutlineState());
		else
			LogStr(WTString("no vaService or outlineFrame"));
	}
	catch (...)
	{
		LogStr(WTString("Exception logging VA Outline."));
	}
}

void VAAutomationLogging::LogFilesComparison(const CStringW& file1, const CStringW& file2)
{
	if (mLoggingEnabled && m_lfile.m_hFile != INVALID_HANDLE_VALUE)
	{
		auto writeStatus = [&](LPCWSTR str) {
			//		ConsoleTrace("%s\r\n",(LPCSTR)CStringA(str));
			LogStrW(str);
		};

		/////////////////////////////////////////////////////////
		// check if files exist

		bool exists1 = ::IsFile(file1);
		bool exists2 = ::IsFile(file2);

		if (!exists1 && !exists2)
		{
			writeStatus(L"Files don't exist.");
			return;
		}
		else if (!exists1)
		{
			writeStatus(L"First file doesn't exist.");
			return;
		}
		else if (!exists2)
		{
			writeStatus(L"Second file doesn't exist.");
			return;
		}

		/////////////////////////////////////////////////////////
		// read files to text strings

		CStringW text1;
		if (!::ReadFileW(file1, text1))
		{
			writeStatus(L"Cannot read first file.");
			return;
		}

		CStringW text2;
		if (!::ReadFileW(file2, text2))
		{
			writeStatus(L"Cannot read second file.");
			return;
		}

		/////////////////////////////////////////////////////////
		// check texts for equality

		if (text1 == text2)
			writeStatus(L"Files are EQUAL.");
		else
			writeStatus(L"Files are DIFFERENT.");
	}
}

void VAAutomationLogging::LogVSResources(WTString category)
{
	try
	{
		if (category.CompareNoCase(_T("KeyBindings")))
		{
			CStringW bindings;

#if !defined(RAD_STUDIO)
			RunFromMainThread([&bindings]() {
				if (g_IdeSettings && !g_IdeSettings->IsLocalized())
					bindings = GetListOfKeyBindingsResourcesForAST();
			});
#endif

			if (!bindings.IsEmpty())
			{
				LogStrW(L"Key bindings resources:");
				LogStrW(bindings);
			}
		}
	}
	catch (...)
	{
		LogStr(WTString("Exception in LogVSResources."));
	}
}

void VAAutomationLogging::LogEditorZoom()
{
	if (!gShellAttr || !gShellAttr->IsDevenv10OrHigher())
		LogStr(WTString("Editor zoom is not supported: Requires vs2010+"));
	else
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
		{
			double zoom_factor = 0;
			RunFromMainThread([&zoom_factor, ed]() { zoom_factor = ed->GetZoomFactor(); });

			WTString str;
			str.WTFormat(_T("Editor Zoom Factor = %d%%"), (int)zoom_factor);
			LogStr(str);
		}
	}
}

void VAAutomationLogging::EnableSourceLinksLogging(bool enable)
{
	if (mSourceLinksLoggingEnabled != enable)
	{
		mSourceLinksLoggingEnabled = enable;

		RunFromMainThread([this]() // must be synced #VaInteropService_Synced
		                  {
			                  if (gVaInteropService)
			                  {
				                  _variant_t vars[2] = {"Logging", mSourceLinksLoggingEnabled};
				                  gVaInteropService->UseInvoker(L"SourceLinksAST", L"Notify", vars, 2);
			                  }
		                  });
	}
}

void VAAutomationLogging::LogMarkers()
{
	try
	{
		LogStr(g_ScreenAttrs.GetSummary());
	}
	catch (...)
	{
		LogStr(WTString("Exception logging markers."));
	}
}

void VAAutomationLogging::LogBraceMarkers()
{
	try
	{
		LogStr(g_ScreenAttrs.GetBraceSummary());
	}
	catch (...)
	{
		LogStr(WTString("Exception logging brace markers."));
	}
}

void VAAutomationLogging::LogMinihelp()
{
	try
	{
		if (g_pMiniHelpFrm)
		{
			CStringW txt(g_pMiniHelpFrm->GetText());
			if (-1 != txt.Find(L"unnamed_"))
			{
				CStringW newTxt;
				LPCWSTR searchFor[] = {L"unnamed_enum_", L"unnamed_struct_", L"unnamed_union_", L""};

				// strip file id from unnamed identifier
				for (int idx = 0; *searchFor[idx]; ++idx)
				{
					int startPos = 0;
					CStringW sf(searchFor[idx]);
					for (;;)
					{
						const int lastStart = startPos;
						startPos = txt.Find(sf, lastStart);
						if (-1 == startPos)
						{
							newTxt += txt.Mid(lastStart);
							break;
						}

						int posNext = txt.Find(L"_", startPos + sf.GetLength() + 1);
						if (-1 == posNext)
						{
							newTxt += txt.Mid(lastStart);
							break;
						}

						newTxt += txt.Mid(lastStart, startPos + sf.GetLength() - lastStart);
						newTxt += L"FileIdRemoved";
						startPos = posNext;
					}

					txt = newTxt;
					newTxt.Empty();
				}
			}

			LogStrW(txt);
		}
		else
			LogStr(WTString("no minihelp"));
	}
	catch (...)
	{
		LogStr(WTString("Exception logging minihelp."));
	}
}

void VAAutomationLogging::LogScope()
{
	try
	{
		EdCntPtr ed = g_currentEdCnt;
		if (ed)
		{
			WTString tmp;
			tmp.WTFormat("Scope: %d %s %s %s\r\n", ed->m_isValidScope, ed->GetSymScope().c_str(), ed->GetSymDef().c_str(),
			             ed->m_lastScope.c_str());
			LogStr(tmp);
		}
		else
			LogStr(WTString("NULL g_currentEdCnt."));
	}
	catch (...)
	{
		LogStr(WTString("Exception logging scope."));
	}
}

void VAAutomationLogging::LogSelection()
{
	try
	{
		EdCntPtr ed = g_currentEdCnt;
		if (ed)
		{
			WTString text;
			RunFromMainThread([&ed, &text]() {
				long p1, p2;
				ed->GetSel2(p1, p2);
				text = "Selection: " + ed->GetSelString();
				if (p1 > p2)
					text += " [Reverse selection]";
			});
			LogStr(text);
		}
		else
			LogStr(WTString("NULL g_currentEdCnt."));
	}
	catch (...)
	{
		LogStr(WTString("Exception logging selection."));
	}
}

void VAAutomationLogging::LogGUIDEqualityInSelection()
{
	try
	{
		EdCntPtr ed = g_currentEdCnt;
		if (ed)
		{
			WTString text;
			RunFromMainThread([&ed, &text]() { text = ed->GetSelString(); });

			LPCWSTR pattern =
			    // format: 8, 4, 4, { 2, 2, 2, 2, 2, 2, 2, 2 }
			    // starts on group: 1
			    L"0x([0-9a-fA-F]{8}),\\s?0x([0-9a-fA-F]{4}),\\s?0x([0-9a-fA-F]{4}),\\s?"
			    L"[{]\\s?"
			    L"0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?"
			    L"0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2})"
			    L"\\s?[}]"

			    L"|"

			    // format: 8, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2
			    // starts on group: 12
			    L"0x([0-9a-fA-F]{8}),\\s?0x([0-9a-fA-F]{4}),\\s?0x([0-9a-fA-F]{4}),\\s?"
			    L"0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?"
			    L"0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2}),\\s?0x([0-9a-fA-F]{2})"

			    L"|"

			    // format: 8-4-4-4-12
			    // starts on group: 23
			    L"([0-9a-fA-F]{8})[-]([0-9a-fA-F]{4})[-]([0-9a-fA-F]{4})[-]([0-9a-fA-F]{4})[-]([0-9a-fA-F]{12})"

			    L"|"

			    // format: 8_4_4_4_12
			    // starts on group: 28
			    L"([0-9a-fA-F]{8})[_]([0-9a-fA-F]{4})[_]([0-9a-fA-F]{4})[_]([0-9a-fA-F]{4})[_]([0-9a-fA-F]{12})";

			CStringW strFirst;
			std::wregex rgx(pattern, std::regex::ECMAScript | std::regex::optimize);

			CStringW wstr = text.Wide();
			LPCWSTR str = wstr;
			LPCWSTR strEnd = str + wstr.GetLength();

			int count = 0;
			bool equal = true;

			std::wcmatch m;
			while (std::regex_search(str, strEnd, m, rgx))
			{
				// determine which part has matched (which GUID format)
				uint group =
				    m[1].matched ? 1u : (m[12].matched ? 12u : (m[23].matched ? 23u : (m[28].matched ? 28u : 0u)));

				if (group != 0)
				{
					count++;

					CStringW wOut;

					do
						wOut.Append(m[group].first, (int)m[group].length());
					while (m[group++].second < m[0].second);

					if (wOut.GetLength() == 32)
					{
						wOut.MakeLower();

						if (strFirst.IsEmpty())
							strFirst = wOut;
						else if (equal && strFirst != wOut)
							equal = false;
					}
					else
					{
						// This will not happen, but could once someone changes
						// pattern without changing expected groups to be matched.
						LogStr(WTString("Invalid GUID length."));
					}
				}
				else
				{
					// This will not happen, but could once someone changes
					// pattern without changing expected groups to be matched.
					LogStr(WTString("Invalid GUID match group."));
				}

				str = m[0].second;
			}

			WTString fmt;
			fmt.WTFormat(_T("%d GUIDs found in selection.\r\n"), count);

			if (count >= 2)
			{
				if (equal)
					fmt.append(_T("GUIDs in selection are equal."));
				else
					fmt.append(_T("GUIDs in selection differ."));
			}
			else
			{
				fmt.append(_T("Not enough data to compare."));
			}

			LogStr(fmt);
		}
		else
			LogStr(WTString("NULL g_currentEdCnt."));
	}
	catch (...)
	{
		LogStr(WTString("Exception logging selection."));
	}
}

CStringW VAAutomationLogging::GetLogPath(CStringW testFile, CStringW testName, BOOL goldResults /*= FALSE*/)
{
	CStringW logDir(GetLogDir(testFile));
	if (goldResults)
		logDir += L"\\ExpectedResults";
	else
		logDir += L"\\" + gShellAttr->GetDbSubDir() + L"Results";
	::CreateDir(logDir);
	return logDir + L"\\" + testName + L".log";
}

void VAAutomationLogging::CheckCurrentTest()
{
	sExecLine = __LINE__;
	CloseLog();

	if (!mCurrentTestInfo)
	{
		ConsoleTraceDirect(kStatusSkippedRepeat);
		return;
	}

	sExecLine = __LINE__;
	CStringW altResults;
	if (mCurrentTestInfo->mLoggingEnabled)
	{
		const CStringW log = GetLogPath(mCurrentTestInfo->mTestSourceFile, mCurrentTestInfo->mTestName, FALSE);
		const CStringW expectedResultLog =
		    GetLogPath(mCurrentTestInfo->mTestSourceFile, mCurrentTestInfo->mTestName, TRUE);
		const CStringW altExpectedResultLog =
		    GetLogPath(mCurrentTestInfo->mTestSourceFile, mCurrentTestInfo->mTestName + L".alt", TRUE);
		const CStringW alt2ExpectedResultLog =
		    GetLogPath(mCurrentTestInfo->mTestSourceFile, mCurrentTestInfo->mTestName + L".alt2", TRUE);
		const CStringW alt3ExpectedResultLog =
		    GetLogPath(mCurrentTestInfo->mTestSourceFile, mCurrentTestInfo->mTestName + L".alt3", TRUE);
		const CStringW alt4ExpectedResultLog =
		    GetLogPath(mCurrentTestInfo->mTestSourceFile, mCurrentTestInfo->mTestName + L".alt4", TRUE);
		if (mSaveNewLog || mSaveAltLog || mSaveAlt2Log || mSaveAlt3Log || mSaveAlt4Log)
		{
			altResults = mSaveAltLog    ? " (alt)"
			             : mSaveAlt2Log ? " (alt2)"
			             : mSaveAlt3Log ? " (alt3)"
			             : mSaveAlt4Log ? " (alt4)"
			                            : "";
			const CStringW expectedResultsLogName(mSaveNewLog    ? expectedResultLog
			                                      : mSaveAltLog  ? altExpectedResultLog
			                                      : mSaveAlt2Log ? alt2ExpectedResultLog
			                                      : mSaveAlt3Log ? alt3ExpectedResultLog
			                                                     : alt4ExpectedResultLog);
			bool edited = false;
			if (::IsFile(expectedResultsLogName) && ::IsFileReadOnly(expectedResultsLogName))
			{
				CString p4cmd;
				CString__FormatA(p4cmd, "p4 edit %s", (LPCTSTR)CString(expectedResultsLogName));
				::system(p4cmd);
				::Sleep(1000);
				edited = true;

				if (mSaveNewLog && ::IsFile(altExpectedResultLog))
					ConsoleTrace("\r\nNote: alt results also exist and may need to be updated\r\n");
			}

			if (::IsFile(log) && (!::IsFile(expectedResultsLogName) || !::IsFileReadOnly(expectedResultsLogName)))
			{
				ConsoleTrace("\r\nUpdating expected results: %ls\r\n", (LPCWSTR)expectedResultsLogName);
				::Copy(log, expectedResultsLogName);
				mCurrentTestInfo->mState = TestInfo::TestPass;

				if (!edited && ::IsFile(expectedResultsLogName))
				{
					CString p4cmd;
					CString__FormatA(p4cmd, "p4 add %s", (LPCTSTR)CString(expectedResultsLogName));
					::system(p4cmd);
				}
			}
			else
			{
				ConsoleTrace("\r\nFailed to update expected results: %ls\r\n", (LPCWSTR)expectedResultsLogName);
				mCurrentTestInfo->mState = TestInfo::TestFail;
			}
		}
		else if (!IsFile(expectedResultLog))
		{
			ConsoleTrace("\r\nMissing expected result: %ls\r\n", (LPCWSTR)expectedResultLog);
			mCurrentTestInfo->mState = TestInfo::TestMissingExpectedResult;
			if (::IsFile(log))
				::CopyFileW(log, expectedResultLog, FALSE);
		}
		else
		{
			WTString actualResults(ReadFile(log));
			if (mNormalizeLogLinebreaks)
				actualResults.ReplaceAll("\r\n", "\n");

			std::tuple<CStringW, CStringW> expectedResultLogs[] = {{expectedResultLog, ""},
			                                                       {altExpectedResultLog, "alt"},
			                                                       {alt2ExpectedResultLog, "alt2"},
			                                                       {
			                                                           alt3ExpectedResultLog,
			                                                           "alt3",
			                                                       },
			                                                       {alt4ExpectedResultLog, "alt4"}};

			WTString expectedResults;
			bool ok = false;
			for (const auto& [l, altstr] : expectedResultLogs)
			{
				for (int i = 0; i < 2; i++)
				{
					CStringW expectedResultLog2 = l;
					CStringW vsver;
					if (i == 0)
					{
						vsver = L"vs" + ::itosw(gShellAttr->GetRegistryVersionNumber());
						expectedResultLog2.Replace(L".log", L"." + vsver + L".log");
					}

					if (!::IsFile(expectedResultLog2))
						continue;

					expectedResults = ::ReadFile(expectedResultLog2);
					if (mNormalizeLogLinebreaks)
						expectedResults.ReplaceAll("\r\n", "\n");

					if (actualResults == expectedResults)
					{
						ok = true;
						if (altstr.GetLength() > 0)
						{
							if (i == 0)
								altResults = L" (" + altstr + L", " + vsver + L")";
							else
								altResults = L" (" + altstr + L")";
						}
						else if (i == 0)
							altResults = L" (" + vsver + L")";
						break;
					}
				}
				if (ok)
					break;
			}

#if 0
			// use for mass update of expected results
			if (actualResults != expectedResults)
			{
				// restrict condition under which mass update occurs as much as possible
				if (/*actualResults.GetLength() == (expectedResults.GetLength() - 1)*/)
				{
					// update expected results
					CString p4cmd;
					p4cmd.Format("p4 edit %s", CString(expectedResultLog));
					::system(p4cmd);
					::Sleep(1000);
					::CopyFileW(log, expectedResultLog, FALSE);

					// reload expected results
					expectedResults = ::ReadFile(expectedResultLog);
					if (mNormalizeLogLinebreaks)
						expectedResults.ReplaceAll("\r\n", "\n");
				}
			}
#endif

			if (!ok)
			{
				if (actualResults.IsEmpty() && !expectedResults.IsEmpty())
					ConsoleTrace("\r\nActual result file is empty: %ls\r\n", (LPCWSTR)log);
				mCurrentTestInfo->mState = TestInfo::TestFail;
			}
			else
				mCurrentTestInfo->mState = TestInfo::TestPass;
		}
	}
	else
	{
		// assume pass if logging is disabled
		ConsoleTrace("\r\nLogging disabled: %ls\r\n", (LPCWSTR)mCurrentTestInfo->mTestName);
		mCurrentTestInfo->mState = TestInfo::TestPass;
	}

#pragma warning(push)
#pragma warning(disable : 4127)
	if (kLimitAutoRunLog && g_AutomatedRun)
		; // Limit logging info, failed tests will be reported at the end of run.
	else if (TestInfo::TestFail == mCurrentTestInfo->mState)
	{
		ConsoleTraceDirect(kStatusFail);
		vCatLog("Editor.Events", "VaEventAT TestFail ***********************************  '%ls'", (LPCWSTR)mCurrentTestInfo->mTestName);
	}
	else
	{
		if (altResults.GetLength() > 0)
			ConsoleTraceDirect(altResults);

		ConsoleTraceDirect(kStatusPass);
	}
#pragma warning(pop)

	mCurrentTestInfo = NULL;
}

void VAAutomationLogging::CompareLogs()
{
	CloseLog();

	for (TestInfoV::iterator it = mTestInfo.begin(); it != mTestInfo.end(); ++it)
	{
		if (!(*it).mLoggingEnabled)
			continue;

		CStringW testName = (*it).mTestName;
		CStringW log = GetLogPath((*it).mTestSourceFile, testName, FALSE);
		WTString actualResults(ReadFile(log));
		if (mNormalizeLogLinebreaks)
			actualResults.ReplaceAll("\r\n", "\n");

		CStringW expectedResultLogs[] = {GetLogPath((*it).mTestSourceFile, testName, true),
		                                 GetLogPath((*it).mTestSourceFile, testName + L".alt", true),
		                                 GetLogPath((*it).mTestSourceFile, testName + L".alt2", true),
		                                 GetLogPath((*it).mTestSourceFile, testName + L".alt3", true),
		                                 GetLogPath((*it).mTestSourceFile, testName + L".alt4", true)};
		CStringW expectedResultLog;
		bool ok = false;
		for (const auto& l : expectedResultLogs)
		{
			for (int i = 0; i < 2; i++)
			{
				CStringW expectedResultLog2 = l;
				if (i == 0)
					expectedResultLog2.Replace(L".log",
					                           L".vs" + ::itosw(gShellAttr->GetRegistryVersionNumber()) + L".log");

				if (!::IsFile(expectedResultLog2))
					continue;
				if (!expectedResultLog.GetLength())
					expectedResultLog = expectedResultLog2;

				WTString expectedResults = ::ReadFile(expectedResultLog2);
				if (mNormalizeLogLinebreaks)
					expectedResults.ReplaceAll("\r\n", "\n");

				if (actualResults == expectedResults)
				{
					ok = true;
					break;
				}
			}
			if (ok)
				break;
		}
		if (ok)
			continue;

		// Log files don't match, run diff on the logs
		RunDiffTool(expectedResultLog, log);
	}
}

void RunDiffTool(CStringW expectedResultLog, CStringW log)
{
	CStringW diffTool;

	const int kBufLen = 2048;
	const std::unique_ptr<WCHAR[]> tmpVec(new WCHAR[kBufLen]);
	WCHAR* tmp = &tmpVec[0];
	*tmp = 0;
	CStringW kDiffToolEnvVar(L"%DiffTool%");
	ExpandEnvironmentStringsW(kDiffToolEnvVar, tmp, kBufLen);
	diffTool = tmp;

	if (diffTool.IsEmpty() || diffTool == kDiffToolEnvVar)
	{
		kDiffToolEnvVar = L"%P4DIFF%";
		ExpandEnvironmentStringsW(kDiffToolEnvVar, tmp, kBufLen);
		diffTool = tmp;
	}

	if (diffTool.IsEmpty() || diffTool == kDiffToolEnvVar)
	{
		if (IsFile(L"C:\\Program Files\\ExamDiff Pro\\ExamDiff.exe"))
			diffTool = L"C:\\Program Files\\ExamDiff Pro\\ExamDiff.exe";
		else if (IsFile(L"C:\\Program Files (x86)\\WinMerge\\WinMergeU.exe")) // I prefer WInmMerge. -Jerry
			diffTool = L"C:\\Program Files (x86)\\WinMerge\\WinMergeU.exe";
		else if (IsFile(L"C:\\Program Files\\WinMerge\\WinMergeU.exe")) // I prefer WInmMerge. -Jerry
			diffTool = L"C:\\Program Files\\WinMerge\\WinMergeU.exe";
		else if (IsFile(L"C:\\Program Files (x86)\\Perforce\\p4merge.exe"))
			diffTool = L"C:\\Program Files (x86)\\Perforce\\p4merge.exe";
		else if (IsFile(L"C:\\Program Files\\Perforce\\p4merge.exe"))
			diffTool = L"C:\\Program Files\\Perforce\\p4merge.exe";
		else
			diffTool = L"p4merge.exe";
	}
	else if (diffTool[0] == L'"' && diffTool[diffTool.GetLength() - 1] == L'"' && diffTool.GetLength() > 2)
		diffTool = diffTool.Mid(1, diffTool.GetLength() - 2);

	CStringW diffCmd;
	CString__FormatW(diffCmd, L"\"%s\" \"%s\" \"%s\"", (LPCWSTR)diffTool, (LPCWSTR)expectedResultLog, (LPCWSTR)log);
	RunProcess(diffCmd, SW_NORMAL);
}

void VAAutomationLogging::TypeString(LPCSTR code, BOOL checkOk)
{
	if (!m_StartTime)
		m_StartTime = GetTickCount();
	CWaitCursor cur;
	// BlockInput(TRUE); // Doesn't work in 2010 and prevents lbutton down from stopping the tests in pre 2010.
	if (!OpenLogFile())
		return;

	__super::TypeString(code, checkOk);
	WaitThreads();
	if (mImplicitHelpers && mLogFinalState)
		LogState(); // LogState if not explicitly directed against it
	CloseLog();
	// BlockInput(FALSE);
	if (m_ACP)
		SpoofACP(m_ACP); // Needs to be cleared "after" logging
	m_ACP = NULL;
}

CStringW VAAutomationLogging::GetLogDir(CStringW testFile)
{
	CStringW logDir;
	if (testFile.IsEmpty())
	{
		_ASSERTE(!"empty string passed to GetLogDir");
		return logDir;
	}

	logDir = ::Path(testFile);
	_ASSERTE(!logDir.IsEmpty());
	_ASSERTE(::IsDir(logDir));
	logDir += "\\AutoTestLogs";
	::CreateDir(logDir);
	return logDir;
}

BOOL VAAutomationLogging::OpenLogFile()
{
	if (m_lfile.m_hFile && (HANDLE)m_lfile.m_hFile != INVALID_HANDLE_VALUE)
		return TRUE; // Already open...

	try
	{
		if (!AddTestRun())
			return FALSE;

		if (mFile.IsEmpty())
		{
			WTString msg;
			msg.WTFormat("\r\nERROR in OpenLogFile: mFile.IsEmpty() for %s\r\n", WTString(m_testName).c_str());
			Log(msg.c_str());
			ConsoleTraceDirect(msg);
			return FALSE;
		}

		m_lfile.Open(GetLogPath(mFile, m_testName),
		             CFile::modeCreate | CFile::shareDenyWrite | CFile::modeWrite | CFile::modeNoInherit);
		if (m_lfile.m_hFile && (HANDLE)m_lfile.m_hFile != INVALID_HANDLE_VALUE)
			return TRUE;

		WTString msg;
		msg.WTFormat("\r\nERROR in OpenLogFile: Open() failed for %s\r\n", WTString(m_testName).c_str());
		Log(msg.c_str());
		ConsoleTraceDirect(msg);
	}
	catch (...)
	{
		WTString msg;
		msg.WTFormat("\r\nException caught in OpenLogFile for %s\r\n", WTString(m_testName).c_str());
		Log(msg.c_str());
		ConsoleTraceDirect(msg);
	}

	return FALSE;
}

void VAAutomationLogging::CloseLog()
{
	if (m_lfile.m_hFile && (HANDLE)m_lfile.m_hFile != INVALID_HANDLE_VALUE)
	{
		m_lfile.Flush();
		m_lfile.Close();

		const DWORD fsize = ::GetFSize(mFile);
		if (!fsize)
		{
			WTString msg;
			msg.WTFormat("\r\nWarning: 0-length file detected for test %s\r\n", WTString(m_testName).c_str());
			Log(msg.c_str());
			ConsoleTraceDirect(msg);
		}
	}
}

void VAAutomationLogging::LogListBox(BOOL selectionState /*= FALSE*/, BOOL exactListboxContents /*= TRUE*/)
{
	if (g_CompletionSet && g_CompletionSet->IsExpUp(g_currentEdCnt))
	{
		WTString listboxcontent;
		WaitUiThreadEvents();
		SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_THREAD_AST_GETLIST, (WPARAM)&listboxcontent, (LPARAM)selectionState);
		if (exactListboxContents || listboxcontent.GetTokCount('\n') < 20)
			LogStr(listboxcontent);
		else
			LogStr("Listbox item count is greater than threshold defined for inexact logging.");
	}
	else
		LogStr("No listbox.");
}

void VAAutomationLogging::LogStr(WTString str)
{
	vCatLog("Editor.Events", "VaEventAT  LogStr  '%s'", str.c_str());
	if (mLoggingEnabled && m_lfile.m_hFile != INVALID_HANDLE_VALUE)
	{
		str += "\r\n";
		m_lfile.Write(str.c_str(), (size_t)str.GetLength());
	}
}

void VAAutomationLogging::LogStrW(CStringW str)
{
	LogStr(WTString(str));
}

void VAAutomationLogging::TraceStr(WTString str)
{
	_ASSERTE(str.GetLength());
	ConsoleTraceDirect(str);
	if (str[str.GetLength() - 1] != '\n')
		ConsoleTrace("\r\n");
}

void VAAutomation::RunTests()
{
	while (g_AutomatedRun && !g_currentEdCnt)
	{
		// CloseAllDocuments to fix for first aspx window not being VA'd.
		if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
			CIdeFind::ShowFindResults1();
		else
			PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "View.FindResults1", 0);

		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Window.CloseAllDocuments", 0);
		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Edit.GoToFindResults1Location", 0);
		Sleep(3000);
	}

	// UndoContext undoContext("AutoTests"); // Messes with 2010 C# intellisense
	if (g_currentEdCnt && GetTestInfo() && IsOK())
	{
		VSOptions TestOptions;
		// #astSetVsDefaults
		// Set initial VS options
		if (!gShellAttr->IsDevenv10OrHigher())
			TestOptions.SetOption("C/C++", "InsertTabs", "1");
		TestOptions.SetOption("CSharp", "InsertTabs", "1");
		if (!gShellAttr->IsDevenv16OrHigher())
			TestOptions.SetOption("CSharp", "IndentStyle", "2");
		TestOptions.SetOption("CSharp-Specific", "BringUpOnIdentifier", "1");
		TestOptions.SetOption("Basic", "InsertTabs", "1");
		TestOptions.SetOption("Basic-Specific", "PrettyListing", "1");
		TestOptions.SetOption("HTML", "InsertTabs", "1");
		TestOptions.SetOption("XAML", "InsertTabs", "0");
		TestOptions.SetOption("HTML Specific", "AttrValueNotQuoted", "0");
		TestOptions.SetOption("HTML Specific", "InsertAttrValueQuotesFormatting", "-1");
		TestOptions.SetOption("HTML Specific", "InsertAttrValueQuotesTyping", "-1");
		TestOptions.SetOption("HTML Specific", "AutoInsertCloseTag", "-1");
		if (!gShellAttr->IsDevenv16OrHigher())
		{
			TestOptions.SetOption("C/C++", "VirtualSpace", "0");
			TestOptions.SetOption("CSharp", "VirtualSpace", "0");
		}

		if (gShellAttr->IsDevenv11OrHigher())
		{
			const WTString commitOn =
			    g_IdeSettings->GetEditorStringOption("C/C++ Specific", "MemberListCommitCharacters");
			if (commitOn.IsEmpty())
				g_IdeSettings->SetEditorOption("C/C++ Specific", "MemberListCommitCharacters",
				                               "{}[]().,:;+-*/%&|^!=<>?@#\\", nullptr);

			if (gShellAttr->IsDevenv12OrHigher() && !gShellAttr->IsDevenv16OrHigher())
				TestOptions.SetOption("C/C++ Specific", "AutoFormatOnSemicolon", "0");

			if (gShellAttr->IsDevenv15OrHigher())
				TestOptions.SetOption("C/C++ Specific", "UseForwardSlashForIncludeAutoComplete", "0");

			::InitVsSettings();
		}

		const BOOL runAllTests = gDirectiveInvokedFrom.Find(kRunAllTestsPrefix) != -1;
		if (runAllTests)
		{
			ConsoleTrace("Batch run type: %s\r\n", gDirectiveInvokedFrom.c_str());
			if (gDirectiveInvokedFrom.Find(kRunAllTestsPrefix) == -1 ||
			    !gShellAttr->IsDevenv16OrHigher()) // [case: 142365]
				g_currentEdCnt = NULL;
			GotoNextTest();
			GetTestInfo();
			if (s_IdeOutputWindow && gShellAttr->IsDevenv10OrHigher())
				s_IdeOutputWindow->Clear();
		}

		const CStringW firstTest = m_testName;
		mFirstFileName = g_currentEdCnt->FileName();

		if (runAllTests)
		{
			CStringW sol;
			if (GlobalProject)
				sol = GlobalProject->SolutionFile();
			ConsoleTrace("Running batch, stop at test %s in\r\n  %s\r\n  %s\r\n", WTString(firstTest).c_str(),
			             WTString(mFirstFileName).c_str(), WTString(sol).c_str());
		}

		try
		{
			if (gReRun.get() && gReRun->mAllowed && GetReRunTestInfo())
			{
				g_AutomatedRun = gReRun->mAutomat ? TRUE : FALSE;
				ReRunFailedTests();
			}
			else
			{
				int stressTestRepeatCnt = 0;
				for (int cnt = 0;; ++cnt)
				{
					bool fireEscapes = false;
					try
					{
						RunTestAtCaret();
					}
					catch (const WtException& e)
					{
						WTString msg;
						msg.WTFormat("\r\nException caught in VAAutomation::RunTests during RunTestAtCaret: %s\r\n",
						             e.GetDesc().c_str());
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						fireEscapes = true;
					}
					catch (...)
					{
						WTString msg;
						msg.WTFormat(
						    "\r\nException in VAAutomation::RunTests during RunTestAtCaret - last exec line was %d\r\n",
						    sExecLine);
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						fireEscapes = true;
#ifndef _WIN64
						throw;
#endif // _WIN64
					}

					if (fireEscapes)
					{
						try
						{
							CheckExceptionDlg();
							Typomatic::TypeString("<esc>"); // Press escape to clear any remaining dialog, using
							                                // Typomatic:: to preserve logging.
							Typomatic::TypeString("<esc>"); // Press escape to clear any remaining dialog, using
							                                // Typomatic:: to preserve logging.
							fireEscapes = false;
						}
						catch (const WtException& e)
						{
							WTString msg;
							msg.WTFormat("\r\nException caught in VAAutomation::RunTests during FireEscapes: %s\r\n",
							             e.GetDesc().c_str());
							Log(msg.c_str());
							ConsoleTraceDirect(msg);
						}
						catch (...)
						{
							WTString msg;
							msg.WTFormat("\r\nException in VAAutomation::RunTests during FireEscapes\r\n");
							Log(msg.c_str());
							ConsoleTraceDirect(msg);
							throw;
						}
					}

					if (fireEscapes)
					{
						try
						{
							if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
								CIdeFind::ShowFindResults1();
							else
								::RunVSCommand("View.FindResults1");

							::Sleep(1000);
							::RunVSCommand("Window.ActivateDocumentWindow");
							::Sleep(1500);
							::RunVSCommand("VAssistX.ReparseCurrentFile");
							::Sleep(1000);
							fireEscapes = false;
						}
						catch (const WtException& e)
						{
							WTString msg;
							msg.WTFormat("\r\nException caught in VAAutomation::RunTests during "
							             "FindResults/Activate/Reparse: %s\r\n",
							             e.GetDesc().c_str());
							Log(msg.c_str());
							ConsoleTraceDirect(msg);
						}
						catch (...)
						{
							WTString msg;
							msg.WTFormat(
							    "\r\nException in VAAutomation::RunTests during FindResults/Activate/Reparse\r\n");
							Log(msg.c_str());
							ConsoleTraceDirect(msg);
							throw;
						}
					}

					if (!IsOK())
						break;

					if (!runAllTests)
					{
						if (VB == gTypingDevLang)
						{
							// workaround for intermittent hangs after one-off VB tests
							// complete and try to display messageBox.
							if (gTestLogger)
							{
								// Press escape to clear any remaining dialog, using Typomatic:: to preserve logging.
								gTestLogger->Typomatic::TypeString("<esc>");
								gTestLogger->Typomatic::TypeString("<esc>");
							}

							if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
								CIdeFind::ShowFindResults1();
							else
								RunVSCommand("View.FindResults1");

							RunVSCommand("Window.CloseToolWindow", FALSE, FALSE);
							RunVSCommand("Window.ActivateDocumentWindow", FALSE, FALSE);
						}
						break;
					}

					try
					{
						CheckExceptionDlg();
						Typomatic::TypeString("<esc>"); // Press escape to clear any remaining dialog, using Typomatic::
						                                // to preserve logging.
					}
					catch (const WtException& e)
					{
						WTString msg;
						msg.WTFormat(
						    "\r\nException caught in VAAutomation::RunTests during Typomatic::TypeString: %s\r\n",
						    e.GetDesc().c_str());
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
					}
					catch (...)
					{
						WTString msg;
						msg.WTFormat("\r\nException caught in VAAutomation::RunTests during Typomatic::TypeString\r\n");
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						throw;
					}

					if (stressTestRepeatCnt && !((stressTestRepeatCnt + cnt) % 50))
					{
						// first pass is normal, subsequent passes will close all
						// windows after every (50 + stressTestRepeatCnt) tests
						ConsoleTrace("StressTest: closing all windows stressCnt(%d) testCnt(%d)\r\n",
						             stressTestRepeatCnt, cnt);
						CloseAllWindowForStressTest(false);
					}

					try
					{
						GotoNextTest();
					}
					catch (const TestOverrunException& e)
					{
						WTString msg;
						msg.WTFormat("\r\nException caught in VAAutomation::RunTests during GotoNextTest: %s\r\n",
						             e.GetDesc().c_str());
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						break;
					}
					catch (const WtException& e)
					{
						WTString msg;
						msg.WTFormat("\r\nException caught in VAAutomation::RunTests during GotoNextTest: %s\r\n",
						             e.GetDesc().c_str());
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						throw;
					}
					catch (...)
					{
						WTString msg;
						msg.WTFormat("\r\nException caught in VAAutomation::RunTests during GotoNextTest\r\n");
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						throw;
					}

					BOOL kFoundTest;
					try
					{
						kFoundTest = GetTestInfo();
					}
					catch (const WtException& e)
					{
						WTString msg;
						msg.WTFormat("\r\nException caught in VAAutomation::RunTests during GetTestInfo: %s\r\n",
						             e.GetDesc().c_str());
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						throw;
					}
					catch (...)
					{
						WTString msg;
						msg.WTFormat("\r\nException caught in VAAutomation::RunTests during GetTestInfo\r\n");
						Log(msg.c_str());
						ConsoleTraceDirect(msg);
						throw;
					}

					if (!kFoundTest || (m_testName == firstTest && g_currentEdCnt &&
					                    !g_currentEdCnt->FileName().CompareNoCase(mFirstFileName)))
					{
						if (runAllTests && kFoundTest && !cnt)
							continue; // try again...

						if (gStressTest)
						{
							// restart loop
							ConsoleTrace("StressTest: restart %d\r\n", ++stressTestRepeatCnt);
							mTestInfo.clear();
							cnt = 0;
							CloseAllWindowForStressTest(true);
							continue;
						}

						if (!kFoundTest)
							ConsoleTrace("Stopping: GetTestInfo failed\r\n");
						break; // Break if we loop back to the first test
					}

					if (cnt > 7000 && !gStressTest)
					{
						// #astIncompleteTestRun
						// this cap was added as a sentry to prevent infinite loop due to either
						// broken Find In Files behavior or broken selection causing repeated run
						// of the loop (unintentional reruns since this is the initial run).
						// added in change 19585 due to vs2012
						ConsoleTrace("ERROR: stopping - bailing out of main test loop\r\n");
						break;
					}
				}
			}
		}
		catch (const WtException& e)
		{
			VALOGEXCEPTION("AST:");
			WTString msg;
			msg.WTFormat("ERROR: exception caught in main VAAutomationThread while loop: %s", e.GetDesc().c_str());
			Log(msg.c_str());
			msg.WTFormat("WtException caught on AST thread.  Tests ending.  Results up to exception will be reported. "
			             "WtException: %s",
			             e.GetDesc().c_str());
			::RunMessageBox(msg.c_str(), MB_OK);
		}
		catch (...)
		{
			VALOGEXCEPTION("AST:");
			Log("ERROR: exception caught in main VAAutomationThread while loop");
			::RunMessageBox("Exception caught on AST thread.  Tests ending.  Results up to exception will be reported.",
			                MB_OK);
		}

		if (m_quit)
			g_AutomatedRun = FALSE; // Clear so IDE doesn't exit when finished.

		if (gStressTest)
		{
			gStressTest = false;
			CString duration;
			const DWORD elapsedTime = ::GetTickCount() - m_StartTime;
			if ((elapsedTime / 60000.0) > 60)
				CString__FormatA(duration, "%.2f hours", elapsedTime / 60000.0 / 60.0);
			else if ((elapsedTime / 1000) > 180)
				CString__FormatA(duration, "%.1f minutes", elapsedTime / 60000.0);
			else
				CString__FormatA(duration, "%ld seconds", elapsedTime / 1000);
			ConsoleTrace("Stress Test ran for %s\r\n", (LPCTSTR)duration);
		}
		else
		{
			while (gExecActive)
			{
				ConsoleTrace("WARN: Test completed but VaService::Exec was active -- waiting longer\r\n");
				::Sleep(20000);
				if (!gExecActive)
				{
					ConsoleTrace("WARN: Test completed but VaService::Exec was active\r\n");
					break;
				}

				ConsoleTrace("ERROR: Test completed but VaService::Exec is active\r\n");
				::WtMessageBox(NULL,
				               "Error after text completion.\r\n"
				               "VaService::Exec is active but shouldn't be.\r\n\r\n"
				               "Pausing for you to clear up bad state/close dialogs.",
				               "AST", MB_OK | MB_ICONSTOP);
			}

			bool hadUnreportedRerun = false;
			const int kMaxRerunLoops = 7;
			int autoRetryCount = g_AutomatedRun ? kMaxRerunLoops : -1; // Retry n times to rerun failed tests
			for (; autoRetryCount; autoRetryCount--)
			{
				CString duration;
				int totalFail = 0, totalMissingExpectedResult = 0;
				GetResults(duration, totalFail, totalMissingExpectedResult);
				hadUnreportedRerun = false;

				CString msg;
				if (totalFail)
				{
					if (totalMissingExpectedResult)
						CString__FormatA(msg,
						                 "Test completed in %s with %d failures (%d tests without gold "
						                 "results).\r\nView failure diffs?",
						                 (LPCTSTR)duration, totalFail, totalMissingExpectedResult);
					else
						CString__FormatA(msg, "Test completed in %s with %d failures.\r\nView failure diffs?",
						                 (LPCTSTR)duration, totalFail);
					// Do not prompt in AutomatedRuns
					if (!g_AutomatedRun && IDYES == ::RunMessageBox(msg, MB_YESNO))
						CompareLogs();

					if (((!g_AutomatedRun && mTestInfo.size() > 1) || (g_AutomatedRun && mTestInfo.size() > 0)) &&
					    (autoRetryCount > 0 ||
					     (!g_AutomatedRun && IDYES == ::RunMessageBox("Rerun failed tests?", MB_YESNO))))
					{
						if (g_AutomatedRun)
						{
							if (totalFail > 300) // if this value changes, also update Test.ps1 (see change 34713)
							{
								// in case of very catastrophic failure, don't even try rerun (to prevent test timeout)
								break;
							}
							else if (totalFail > 200 && autoRetryCount < kMaxRerunLoops)
							{
								// in case of catastrophic failure, limit rerun to a single occurrence
								break;
							}

							hadUnreportedRerun = true;
						}

						ReRunFailedTests();
						continue;
					}
				}
				else if (!g_AutomatedRun)
				{
					if (totalMissingExpectedResult)
						CString__FormatA(msg, "Test completed in %s (no failures, %d tests without gold results).",
						                 (LPCTSTR)duration, totalMissingExpectedResult);
					else
						CString__FormatA(msg, "Test completed in %s (no failures).", (LPCTSTR)duration);

					::RunMessageBox(msg, MB_OK);
				}

				break;
			}

			if (hadUnreportedRerun)
			{
				// display results of final run
				CString duration;
				int totalFail = 0, totalMissingExpectedResult = 0;
				GetResults(duration, totalFail, totalMissingExpectedResult);
			}
		}
	}

	NotifyTestsActive(false);
}

static int GetIdeVer()
{
	static int ideVer = 0;
	if (ideVer)
		return ideVer;
	if (gShellAttr)
		ideVer = gShellAttr->GetRegistryVersionNumber();
	return ideVer;
}

bool HandleSkipTagInTestCode(WTString& m_testCode)
{
	const WTString kSkip("<skip:");
	while (m_testCode.FindNoCase(kSkip) == 0)
	{
		WTString skipDirective(m_testCode.Mid(kSkip.GetLength()));
		int pos = skipDirective.Find('>');
		if (-1 == pos)
			return true;

		m_testCode = m_testCode.Mid(kSkip.GetLength() + pos + 1);

		skipDirective = skipDirective.Left(pos);

		if (skipDirective.length() > 4 && !StrNCmpI(skipDirective.c_str(), "LCID", 4))
		{
			int eqPos = skipDirective.find('=');
			if (eqPos >= 0)
			{
				// support also "==" operator
				if (eqPos + 1 < skipDirective.length() && skipDirective[eqPos + 1] == '=')
					++eqPos;

				// handle "!=" which negates result
				bool negate = eqPos > 0 && skipDirective[eqPos - 1] == '!';

				// we will compare to LCID of IDE
				LCID vsLcid = g_IdeSettings ? g_IdeSettings->GetLocaleID() : 1033;

				// get the rest of directive and trim it
				skipDirective = skipDirective.Mid(eqPos + 1);
				skipDirective.Trim();
				token lcidList = skipDirective;

				// prepare the list of LCIDs to be considered
				std::set<LCID> lcidSet;
				for (WTString tok = lcidList.read(",;"); !tok.IsEmpty(); tok = lcidList.read(",;"))
				{
					tok.Trim();
					LCID lcid = _tcstoul(tok.c_str(), nullptr, 0);
					if (lcid != 0)
						lcidSet.insert(lcid);
				}

				bool isListed = lcidSet.find(vsLcid) != lcidSet.end();

				return negate ? !isListed : isListed;
			}

			_ASSERTE(!"Invalid LCID directive, valid format is: <Skip:LCID[=|==|!=]#;#;...>, for example: "
			          "<Skip:LCID!=1033>");

			return false;
		}

		if (skipDirective.GetLength() > 2)
		{
			WTString op = skipDirective.Left(2);
			op.MakeLower();
			WTString verStr = skipDirective.Mid(2);
			int skipVer = atoi(verStr.c_str());
			int ideVer = GetIdeVer();
			if (skipVer && ideVer)
			{
				if (op == "eq")
				{
					if (ideVer == skipVer)
						return true;
				}
				else if (op == "ne")
				{
					if (ideVer != skipVer)
						return true;
				}
				else if (op == "lt")
				{
					if (ideVer < skipVer)
						return true;
				}
				else if (op == "le")
				{
					if (ideVer <= skipVer)
						return true;
				}
				else if (op == "gt")
				{
					if (ideVer > skipVer)
						return true;
				}
				else if (op == "ge")
				{
					if (ideVer >= skipVer)
						return true;
				}
			}
		}

		if (skipDirective == "6")
		{
			if (gShellAttr->IsMsdev())
				return true;
		}
		else if (skipDirective == "6+")
		{
			_ASSERTE(!"this directive is meaningless - delete the test if you don't want it to run");
			return true;
		}
		else if (skipDirective == "6-")
		{
			_ASSERTE(!"this directive is meaningless because ast doesn't run in IDEs less than 6");
		}
		else if (skipDirective == "7")
		{
			if (gShellAttr->IsDevenv7())
				return true;
		}
		else if (skipDirective == "7+")
		{
			if (gShellAttr->IsDevenv())
				return true;
		}
		else if (skipDirective == "7-")
		{
			if (!gShellAttr->IsDevenv())
				return true;
		}
		else if (skipDirective == "8")
		{
			if (gShellAttr->IsDevenv8())
				return true;
		}
		else if (skipDirective == "8+")
		{
			if (gShellAttr->IsDevenv8OrHigher())
				return true;
		}
		else if (skipDirective == "8-")
		{
			if (!gShellAttr->IsDevenv8OrHigher())
				return true;
		}
		else if (skipDirective == "9")
		{
			if (gShellAttr->IsDevenv9())
				return true;
		}
		else if (skipDirective == "9+")
		{
			if (gShellAttr->IsDevenv9OrHigher())
				return true;
		}
		else if (skipDirective == "9-")
		{
			if (!gShellAttr->IsDevenv9OrHigher())
				return true;
		}
		else if (skipDirective == "10")
		{
			if (gShellAttr->IsDevenv10())
				return true;
		}
		else if (skipDirective == "10+")
		{
			if (gShellAttr->IsDevenv10OrHigher())
				return true;
		}
		else if (skipDirective == "10-")
		{
			if (!gShellAttr->IsDevenv10OrHigher())
				return true;
		}
		else if (skipDirective == "11")
		{
			if (gShellAttr->IsDevenv11())
				return true;
		}
		else if (skipDirective == "11+")
		{
			if (gShellAttr->IsDevenv11OrHigher())
				return true;
		}
		else if (skipDirective == "11-")
		{
			if (!gShellAttr->IsDevenv11OrHigher())
				return true;
		}
		else if (skipDirective == "12")
		{
			if (gShellAttr->IsDevenv12())
				return true;
		}
		else if (skipDirective == "12+")
		{
			if (gShellAttr->IsDevenv12OrHigher())
				return true;
		}
		else if (skipDirective == "12-")
		{
			if (!gShellAttr->IsDevenv12OrHigher())
				return true;
		}
		else if (skipDirective == "14")
		{
			if (gShellAttr->IsDevenv14())
				return true;
		}
		else if (skipDirective == "14+")
		{
			if (gShellAttr->IsDevenv14OrHigher())
				return true;
		}
		else if (skipDirective == "14-")
		{
			if (!gShellAttr->IsDevenv14OrHigher())
				return true;
		}
		else if (skipDirective == "15")
		{
			if (gShellAttr->IsDevenv15())
				return true;
		}
		else if (skipDirective == "15+")
		{
			if (gShellAttr->IsDevenv15OrHigher())
				return true;
		}
		else if (skipDirective == "15-")
		{
			if (!gShellAttr->IsDevenv15OrHigher())
				return true;
		}
		else if (skipDirective == "16")
		{
			if (gShellAttr->IsDevenv16())
				return true;
		}
		else if (skipDirective == "16+")
		{
			if (gShellAttr->IsDevenv16OrHigher())
				return true;
		}
		else if (skipDirective == "16-")
		{
			if (!gShellAttr->IsDevenv16OrHigher())
				return true;
		}
		else if (skipDirective == "17")
		{
			if (gShellAttr->IsDevenv17())
				return true;
		}
		else if (skipDirective == "17+")
		{
			if (gShellAttr->IsDevenv17OrHigher())
				return true;
		}
		else if (skipDirective == "17-")
		{
			if (!gShellAttr->IsDevenv17OrHigher())
				return true;
		}
		// #newVsVersion
	}

	return false;
}

bool VAAutomation::RunTestAtCaret()
{
	sExecLine = __LINE__;
	if (PopupMenuXP::IsMenuActive())
	{
		ConsoleTrace("ERROR: starting new test while menu is active (A)\r\n");
		PopupMenuXP::CancelActiveMenu();
	}

	sExecLine = __LINE__;
	if (gExternalMenuCount)
	{
		ConsoleTrace("ERROR: starting new test while external menu is active (A)\r\n");
		gTestLogger->Typomatic::TypeString("<esc>", FALSE);
	}

	sExecLine = __LINE__;
	if (PopupMenuXP::ActiveMenuCount() || gExternalMenuCount)
		ConsoleTrace("ERROR: starting new test while menu is active (B)\r\n");

	sExecLine = __LINE__;
	if (::IsMinihelpDropdownVisible())
	{
		::HideMinihelpDropdown();
		WTString msg;
		msg.WTFormat("ERROR: starting new test while MIF list dropped (previous test: %s) %d\r\n",
		             WTString(m_testName).c_str(), IsMinihelpDropdownVisible());
		ConsoleTraceDirect(msg);
	}

	// User selected "VAAutoTest:" scan line to get and type string...
	sExecLine = __LINE__;
	m_TypingDelay = DEFAULT_TYPING_DELAY;
	if (!GetTestInfo())
		return false;

	vCatLog("Editor.Events", "VaEventAT RunTest ***********  '%s'", WTString(m_testName).c_str());
	// UndoContext undoContext("AutoTest"); // Messes with 2010 C# intellisense
	sExecLine = __LINE__;
	{
		EdCntPtr ed(g_currentEdCnt);
		RunFromMainThread([&ed, this]() { ed->SetPos(m_testStartPos); });
	}
	if (m_testCode.IsEmpty())
		return false;

	// check for IDE specific tests
	sExecLine = __LINE__;
	if (HandleSkipTagInTestCode(m_testCode))
		return false;

	// Run the test...
	sExecLine = __LINE__;
	ResetLoggingOptions();

	struct notifyAboutTest
	{
		const CStringW& _name;
		notifyAboutTest(const CStringW& name)
		    : _name(name)
		{
			RunFromMainThread([this]() // must be synced #VaInteropService_Synced
			                  {
				                  if (gVaInteropService)
					                  gVaInteropService->NotifyTestStartingEnding(_name, false,
					                                                              g_AutomatedRun ? true : false);
			                  });
		}

		~notifyAboutTest()
		{
			RunFromMainThread([this]() // must be synced #VaInteropService_Synced
			                  {
				                  if (gVaInteropService)
					                  gVaInteropService->NotifyTestStartingEnding(_name, true,
					                                                              g_AutomatedRun ? true : false);
			                  });
		}
	} _notifyAboutTest(m_testName);

	if (!g_AutomatedRun || !kLimitAutoRunLog)
		ConsoleTrace("test: %s", WTString(m_testName).c_str());
	sExecLine = __LINE__;
	if (mImplicitHelpers && m_InsertLineBelow)
		RunVSCommand("Edit.LineOpenBelow");
	sExecLine = __LINE__;
	EdCntPtr ed(g_currentEdCnt);
	RunFromMainThread([&ed, this]() {
		uint cp = ed->CurPos();
		m_testStartPos = (uint)ed->GetBufIndex((long)cp);
	});
	sExecLine = __LINE__;
	TypeString(m_testCode.c_str());
	sExecLine = __LINE__;
	CheckCurrentTest();
	sExecLine = __LINE__;
	if (gTestSnippetSubDir.GetLength())
	{
		gTestSnippetSubDir.Empty();
		gAutotextMgr->Load(NULL);           // To force loading of old snippets
		gAutotextMgr->Load(gTypingDevLang); // Reset old snippets
	}

	return true;
}

void VAAutomation::GotoNextTest()
{
	while (gExecActive)
	{
		ConsoleTrace("WARN: GotoNextTest called but VaService::Exec was active -- waiting longer\r\n");
		::Sleep(20000);
		if (!gExecActive)
		{
			ConsoleTrace("WARN: GotoNextTest called but VaService::Exec was active\r\n");
			break;
		}

		ConsoleTrace("ERROR: GotoNextTest called during VaService::Exec\r\n");
		::WtMessageBox(NULL,
		               "Error during GotoNextTest.\r\n"
		               "VaService::Exec is active but shouldn't be.\r\n\r\n"
		               "Pausing for you to clear up bad state/close dialogs.",
		               "AST", MB_OK | MB_ICONSTOP);
	}

	static CStringW sLastTestDocument;
	::RunVSCommand("Edit.GoToFindResults1NextLocation", FALSE);
	CStringW curTestDocument = ::GetActiveDocumentSafe();
	if (curTestDocument.IsEmpty())
	{
		::Sleep(2000);
		curTestDocument = ::GetActiveDocumentSafe();
		if (curTestDocument.IsEmpty())
		{
			ConsoleTrace("ERROR: GotoNextTest empty curTestDocument\r\n");
			if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
				CIdeFind::ShowFindResults1();
			else
				::RunVSCommand("View.FindResults1");

			::Sleep(500);
			GotoNextTest();
			return;
		}
	}

	if (-1 != curTestDocument.Find(L"ast.txt"))
	{
		GotoNextTest();
		return;
	}

	if (-1 != curTestDocument.Find(L"\\AutoTestLogs\\"))
	{
		ConsoleTrace("skipping find result in %s\r\n", (LPCTSTR)CString(curTestDocument));
		GotoNextTest();
		return;
	}

	if (!sLastTestDocument.IsEmpty() && !sLastTestDocument.CompareNoCase(curTestDocument) && g_currentEdCnt)
		return;

	if (sLastTestDocument.IsEmpty() || sLastTestDocument.CompareNoCase(curTestDocument))
	{
		if (curTestDocument.CompareNoCase(mFirstFileName) && !gStressTest)
		{
			const DWORD kLoopStartTime = ::GetTickCount();
			uint fid = gFileIdManager->GetFileId(curTestDocument);
			// This loop it introduced in change 16259.
			// AddTestRun was modified in change 19964.
			// The changes made in 19964 make this loop not so important anymore.
			// Might consider removing the entire loop.
			for (int cnt = 0; mFilesTested.Contains(fid); ++cnt)
			{
				ConsoleTrace("WARN: GotoNextTest skipping repeat in %s ed(%p) last(%s)\r\n",
				             (LPCTSTR)CString(curTestDocument), g_currentEdCnt.get(),
				             (LPCTSTR)CString(sLastTestDocument));

				// sometimes VS will list results for a file twice.
				// tests for the second set will fail if the file was modified by the first pass.
				// skip all the tests in this file if we've already added it to the list.
				::RunVSCommand("Edit.GoToFindResults1NextLocation", FALSE);
				curTestDocument = ::GetActiveDocumentSafe();
				if (!curTestDocument.CompareNoCase(mFirstFileName))
				{
					// let the run loop handle test termination (when curTestDocument == mFirstFileName)
					break;
				}

				fid = gFileIdManager->GetFileId(curTestDocument);

				if (cnt > 100)
					throw TestOverrunException("GotoNextTest skipped too many items -- bailing out");

				if ((::GetTickCount() - kLoopStartTime) > (5 * 60000))
				{
					ConsoleTrace("ERROR: GotoNextTest bad loop start(%s) cur(%s)\r\n", (LPCTSTR)CString(mFirstFileName),
					             (LPCTSTR)CString(curTestDocument));
					return;
				}
			}
		}

		// 		ConsoleTrace("File change: GotoNextTest moved to %s ed(%08x) last(%s)\r\n",
		// 			CString(curTestDocument), g_currentEdCnt.get(), CString(sLastTestDocument));
		mFilesTested.Add(curTestDocument);
		sLastTestDocument = curTestDocument;

		if (GlobalProject->GetContainsUnrealEngineProject())
		{
			CStringW baseNameNoExt = GetBaseNameNoExt(curTestDocument);
			if (baseNameNoExt.Find(L"_delay") == baseNameNoExt.GetLength() - 6)
			{
				// [case: 119201] [testing] add wait when opening new ue4 test files to allow the delayed intellisense
				// parse to finish
				Sleep(60000);
			}
		}

		if (!(mFilesTested.size() % kMaxFileOpenCount))
		{
			CloseAllDocuments();

			::RunVSCommand("Edit.GoToFindResults1Location", FALSE);
			curTestDocument = ::GetActiveDocumentSafe();
			if (curTestDocument.IsEmpty())
			{
				::Sleep(2000);
				curTestDocument = ::GetActiveDocumentSafe();
				if (curTestDocument.IsEmpty())
				{
					ConsoleTrace("ERROR: GotoNextTest empty curTestDocument\r\n");

					if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
						CIdeFind::ShowFindResults1();
					else
						::RunVSCommand("View.FindResults1");

					::Sleep(500);
					GotoNextTest();
					return;
				}
			}

			_ASSERTE(curTestDocument == sLastTestDocument);
		}
	}

	try
	{
		if (g_currentEdCnt && !g_currentEdCnt->FileName().CompareNoCase(sLastTestDocument))
			return;
	}
	catch (...)
	{
	}

	const int kMaxAttempts = 7;
	int curAttempts = 0;
	do
	{
		try
		{
			CheckExceptionDlg();
			Typomatic::TypeString("<esc>", FALSE);
			Typomatic::TypeString("<esc>", FALSE);
		}
		catch (...)
		{
		}

		if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
			CIdeFind::ShowFindResults1();
		else
			::RunVSCommand("View.FindResults1");

		::Sleep(1000);
		::RunVSCommand("Window.ActivateDocumentWindow");
		::Sleep(1500);
		curTestDocument = ::GetActiveDocumentSafe();
		if (++curAttempts > kMaxAttempts)
			break;
	} while (!g_currentEdCnt || (g_currentEdCnt->FileName().CompareNoCase(sLastTestDocument) && !gStressTest));

	if (curAttempts > kMaxAttempts)
	{
		ConsoleTrace("ERROR: GotoNextTest failed in %s (last=%s), moving on (ed=%p)\r\n",
		             (LPCTSTR)CString(curTestDocument), (LPCTSTR)CString(sLastTestDocument), g_currentEdCnt.get());
		GotoNextTest();
		return;
	}

	// make sure CurPos() is correct by sending some arrow keys
	VAAutomationOptions::TypeString("<RIGHT><LEFT>"); // no logging
}

void VAAutomation::CloseAllDocuments()
{
	// in vs2015, don't leave more than xx documents open due to OutOfMemoryExceptions
	if (gShellAttr->IsDevenv16OrHigher())
		::Sleep(1000);

	if (gVaAddinClient.IsExecutingDteCommand())
	{
		if (gShellAttr->IsDevenv16u11OrHigher())
			ClosePersistedUI();
		else
			Typomatic::TypeString("n"); // Press 'n' to not save all document, using Typomatic:: to preserve logging.

		::Sleep(2000);

		if (g_AutomatedRun && gShellAttr->IsDevenv16u11OrHigher() && gVaAddinClient.IsExecutingDteCommand())
		{
			std::string prev_cmds;
			for (int attempts = INT_MAX; attempts--;)
			{
				if (!ClosePersistedUI())
					Typomatic::TypeString("<esc>", FALSE);

				::Sleep(2000);

				if (!gVaAddinClient.IsExecutingDteCommand())
					break;

				auto cmds = gVaAddinClient.GetExecutingDteCommands();
				if (prev_cmds != cmds)
				{
					ConsoleTrace("Waiting for DTE command(s) to finish: %s\r\n", cmds.c_str());
					prev_cmds = cmds;
				}
			}
		}

		if (gVaAddinClient.IsExecutingDteCommand())
			::WtMessageBox("CloseAllDocuments called while VA is still in exec of DTE command",
			               "Visual Assist AST", 0);
	}

	// Window.CloseAllDocuments has to be exec'd as a PostMessage because if it is
	// SendMessage, then the dlg will block until someone responds to it.
	::RunVSCommand("Window.CloseAllDocuments", FALSE, TRUE); // Don't block until user presses y/n to save all

	const int kMAxAttempts = 3;
	for (int attempts = 0; attempts < kMAxAttempts; ++attempts)
	{
		try
		{
			if (gShellAttr->IsDevenv16OrHigher())
				::Sleep(2000);

			WaitDlg(kShellNameUsedInCloseAll);
			::Sleep(4000);
			Typomatic::TypeString("n"); // Press 'n' to not save all document, using Typomatic:: to preserve logging.
			break;
		}
		catch (const WtException&)
		{
			// no save dlg displayed
			ConsoleTrace("CloseAllDocuments, no save prompt\r\n");

			if (!gVaAddinClient.IsExecutingDteCommand())
				break;

			::Sleep(4000);
			Typomatic::TypeString("n"); // Press 'n' to not save all document, using Typomatic:: to preserve logging.

			::Sleep(1000);
			if (!gVaAddinClient.IsExecutingDteCommand())
				break;

			if (attempts == kMAxAttempts - 1)
				::WtMessageBox("CloseAllDocuments dialog failed to be found?", "Visual Assist AST", 0);
		}
	}

	::Sleep(2000);
	if (gShellAttr->IsDevenv15OrHigher() && !gShellAttr->IsDevenv15u8OrHigher())
		::RunVSCommand("Tools.ForceGC", FALSE, FALSE);

	::WaitThreads(FALSE);

	if (gShellAttr->IsDevenv15OrHigher() && !gShellAttr->IsDevenv15u8OrHigher())
	{
		::WaitUiThreadEvents();
		ConsoleTrace("Waiting extra long after closing all documents in VS2017 (< 15.8)...\r\n");
		::Sleep(5000);
	}

	::DumpProcessStats();
}

void VAAutomation::CloseAllWindowForStressTest(bool gotoNext)
{
	RunVSCommand("Window.CloseAllDocuments", FALSE, TRUE); // Automatically close all documents w/o saving
	::Sleep(1000);
	// Press 'n' to not save all documents, using SimulateKeyPress to preserve logging.
	SimulateKeyPress('n');
	::Sleep(500);
	g_currentEdCnt = NULL;
	mFilesTested.clear();

	::RunVSCommand("View.FindResults1");
	::Sleep(500);

	if (gotoNext)
	{
		GotoNextTest();
		GetTestInfo();
		if (s_IdeOutputWindow && gShellAttr->IsDevenv10OrHigher())
			s_IdeOutputWindow->Clear();

		WaitThreads(TRUE);
	}
}

bool VAAutomation::ClosePersistedUI()
{
	bool closed_any = false;

	HWND wnd = GetForegroundWindow();
	if (wnd && ::IsWindowVisible(wnd))
	{
		GUITHREADINFO inf;
		ZeroMemory(&inf, sizeof(GUITHREADINFO));
		inf.cbSize = sizeof(GUITHREADINFO);
		if (::GetGUIThreadInfo(g_mainThread, &inf) && wnd == inf.hwndActive)
		{
			WTString wndText(GetWindowTextString(wnd));
			if (wndText == kShellNameUsedInCloseAll)
			{
				ConsoleTrace("Warn: last test left VS dialog open\r\n");
				::PostMessage(wnd, WM_COMMAND, IDNO, 0); // press [Don't save] button
				closed_any = true;
			}
		}
	}

	if (PopupMenuXP::IsMenuActive())
	{
		ConsoleTrace("Warn: last test left MenuXP open\r\n");
		PopupMenuXP::CancelActiveMenu();
		closed_any = true;
	}

	if (::IsMinihelpDropdownVisible())
	{
		ConsoleTrace("Warn: last test left Minihelp dropdown open\r\n");
		::HideMinihelpDropdown();
		closed_any = true;
	}

	if (gExternalMenuCount)
	{
		ConsoleTrace("Warn: last test left external menu open\r\n");
		Typomatic::TypeString("<esc>", FALSE);
		closed_any = true;
	}

	return closed_any;
}

void VAAutomationThread(LPVOID waitBeforeStart)
{
	_ASSERTE(gTestsActive);
	LOG2("VAAutomationThread::Run");
	HANDLE hThrd = GetCurrentThread(); // don't need to call CloseHandle
	const int prevPriority = GetThreadPriority(hThrd);
	// temporarily disable screensaver and system sleep
	const EXECUTION_STATE prevExecState =
	    SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED | ES_CONTINUOUS);
	SetThreadPriority(hThrd, THREAD_PRIORITY_LOWEST);
	if (g_AutomatedRun)
	{
		LogVersionInfo inf(true);
		CString info("Environment info:\r\n");
		info += GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "AboutInfo");
		CString tmp;
		CString__FormatA(tmp, "code inspection: %d\r\n", Psettings->mCodeInspection);
		info += tmp;
		CString__FormatA(tmp, "pid %ld\r\n\r\n", GetCurrentProcessId());
		info += tmp;
		ConsoleTraceDirect(info);

		DumpProcessStats();
		Sleep(1000);
	}
	else if (waitBeforeStart)
		Sleep(500);

	if (gVaShellService && gShellAttr && gShellAttr->IsDevenv10OrHigher() && !gShellAttr->IsDevenv17OrHigher())
	{
		// [case: 120041]
		// don't continue until status bar no longer indicates that
		// Find is running.  There's probably an IVs* interface that
		// is better suited to this...
		CString currentTxt;
		for (;;)
		{
			currentTxt = gVaShellService->GetStatusText();
			if (-1 != currentTxt.Find("Searching") || -1 != currentTxt.Find("Find all"))
			{
				Sleep(1000);
				DumpProcessStats();
			}
			else
				break;
		}
	}

	while (!IsAnySysDicLoaded() || !GlobalProject || GlobalProject->IsBusy())
	{
		Sleep(1000);
		DumpProcessStats();
	}

	{
		// [case: 131333]
		// wait for below normal priority parsing to complete so that WaitThreads doesn't throw (it times out at 2
		// minutes)
		const DWORD startTicks = GetTickCount();
		while (g_ParserThread->HasQueuedItems() || g_ParserThread->IsNormalJobActiveOrPending() ||
		       g_ParserThread->HasBelowNormalJobActiveOrPending())
		{
			Sleep(1000);
			DumpProcessStats();

			const int kMinutes = 30;
			if (GetTickCount() > startTicks + (kMinutes * 60000))
			{
				// bail
				const char* msg = "ERROR: VAAutomationThread timed out waiting for parserThread";
				Log(msg);
				ConsoleTraceDirect(msg);
				break;
			}
		}
	}

	if (g_AutomatedRun)
		DumpProcessStats();

	// load before tests start [case: 47141]
	gAutotextMgr->Unload();
	gAutotextMgr->Load(gTypingDevLang);
	ClassViewSym.Empty(); // Reset cached HCB symbol.
	if (g_CVAClassView)
	{
		g_CVAClassView->m_lock = FALSE;
		g_CVAClassView->ClearHcb(false);
	}

	try
	{
		VAAutomation joeTypist;
		Sleep(500);
		TempAssign<VAAutomationLogging*> tmp(gTestLogger, &joeTypist);
		joeTypist.RunTests();

		if (g_AutomatedRun)
			joeTypist.CloseAllDocuments();
	}
	catch (...)
	{
		VALOGEXCEPTION("AST:");
		Log("ERROR: exception caught in VAAutomationThread");
		::RunMessageBox("Exception caught on AST thread.  Tests ending.", MB_OK);
	}
	SetThreadPriority(hThrd, prevPriority);
	SetThreadExecutionState(prevExecState);
	NotifyTestsActive(false);

	// restore user snippets (after gTestsActive is cleared)
	gAutotextMgr->Unload();
	gAutotextMgr->Load(gTypingDevLang);
	if (g_AutomatedRun)
	{
		// #astAutomaticShutdown --
		// this way of closing IDE might need to be revisited if we continue to
		// see strange behavior after test completion in automated environments
		// (like log file rename failures, hung IDEs, etc)
		gMainWnd->PostMessage(WM_CLOSE);
		Sleep(100);
		SimulateKeyPress('N'); // Press 'n' to not save macros
	}
}

bool FindTests(EnvDTE::vsFindTarget fndTgt)
{
	CComPtr<EnvDTE::Find> iFinder;
	HRESULT res = gDte->get_Find(&iFinder);
	if (!iFinder)
		return false;

	// save previous values
	CComBSTR prevFindWhat;
	iFinder->get_FindWhat(&prevFindWhat);
	EnvDTE::vsFindPatternSyntax prevSyntax;
	iFinder->get_PatternSyntax(&prevSyntax);
	EnvDTE::vsFindResultsLocation prevLocation;
	res = iFinder->get_ResultsLocation(&prevLocation);
	EnvDTE::vsFindTarget prevTarget;
	res = iFinder->get_Target(&prevTarget);
	CComBSTR prevFilter;
	res = iFinder->get_FilesOfType(&prevFilter);
	EnvDTE::vsFindAction prevAction;
	res = iFinder->get_Action(&prevAction);

	// do find
	res = iFinder->put_FindWhat(CComBSTR(VA_AUTOTES_STR));
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_PatternSyntax(EnvDTE::vsFindPatternSyntaxLiteral);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_ResultsLocation(EnvDTE::vsFindResults1);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_Target(fndTgt);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_FilesOfType(CComBSTR(L"*.*"));
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_Action(EnvDTE::vsFindActionFindAll);
	_ASSERTE(SUCCEEDED(res));

	bool retval = false;
	EnvDTE::vsFindResult findRes = EnvDTE::vsFindResultError;
	res = iFinder->Execute(&findRes);
	_ASSERTE(SUCCEEDED(res));
	if (findRes == EnvDTE::vsFindResultFound)
	{
		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Edit.GoToFindResults1NextLocation", 0);
		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Window.ActivateDocumentWindow", 0);
		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Edit.GoToFindResults1Location", 0);
		retval = true;
	}

	// restore prev values
	res = iFinder->put_FindWhat(prevFindWhat);
	res = iFinder->put_PatternSyntax(prevSyntax);
	res = iFinder->put_ResultsLocation(prevLocation);
	res = iFinder->put_Target(prevTarget);
	res = iFinder->put_FilesOfType(prevFilter);
	res = iFinder->put_Action(prevAction);

	return retval;
}

BOOL WINAPI IdeFindMainThread(LPCSTR pattern, BOOL regexp /*= FALSE*/)
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	CComPtr<EnvDTE::Find> iFinder;
	HRESULT res = gDte->get_Find(&iFinder);
	if (!iFinder)
	{
		ConsoleTrace("ERROR: failed to load DTE::Find\r\n");
		return false;
	}

	// save previous values
	CComBSTR prevFindWhat;
	iFinder->get_FindWhat(&prevFindWhat);
	EnvDTE::vsFindPatternSyntax prevSyntax;
	iFinder->get_PatternSyntax(&prevSyntax);
	EnvDTE::vsFindResultsLocation prevLocation;
	res = iFinder->get_ResultsLocation(&prevLocation);
	EnvDTE::vsFindTarget prevTarget;
	res = iFinder->get_Target(&prevTarget);
	CComBSTR prevFilter;
	res = iFinder->get_FilesOfType(&prevFilter);
	EnvDTE::vsFindAction prevAction;
	res = iFinder->get_Action(&prevAction);
	VARIANT_BOOL prevDirection;
	res = iFinder->get_Backwards(&prevDirection);
	VARIANT_BOOL prevCase;
	res = iFinder->get_MatchCase(&prevCase);
	VARIANT_BOOL prevWholeWord;
	res = iFinder->get_MatchWholeWord(&prevWholeWord);

	// do find
	res = iFinder->put_FindWhat(CComBSTR(pattern));
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_ResultsLocation(EnvDTE::vsFindResultsNone);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_Target(EnvDTE::vsFindTargetCurrentDocument);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_PatternSyntax(regexp ? EnvDTE::vsFindPatternSyntaxRegExpr : EnvDTE::vsFindPatternSyntaxLiteral);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_MatchCase(FALSE);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_Backwards(FALSE);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_MatchWholeWord(FALSE);
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_FilesOfType(CComBSTR(L"*.*"));
	_ASSERTE(SUCCEEDED(res));
	res = iFinder->put_Action(EnvDTE::vsFindActionFind);
	_ASSERTE(SUCCEEDED(res));

	EnvDTE::vsFindResult findRes = EnvDTE::vsFindResultError;
	res = iFinder->Execute(&findRes);
	_ASSERTE(SUCCEEDED(res));

	// restore prev values
	res = iFinder->put_FindWhat(prevFindWhat);
	res = iFinder->put_PatternSyntax(prevSyntax);
	res = iFinder->put_ResultsLocation(prevLocation);
	res = iFinder->put_Target(prevTarget);
	res = iFinder->put_FilesOfType(prevFilter);
	res = iFinder->put_Action(prevAction);
	res = iFinder->put_Backwards(prevDirection);
	res = iFinder->put_MatchCase(prevCase);
	res = iFinder->put_MatchWholeWord(prevWholeWord);

	return findRes == EnvDTE::vsFindResultFound;
}

BOOL IdeFind(LPCSTR pattern, BOOL regexp /*= FALSE*/)
{
	// [case: 61671] if file is modified, find can return success but not
	// actually work. give dte a chance to sync.
	Sleep(250);

	// 	if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
	// 		return IdeFind17(pattern, regexp);

	return RunFromMainThread<BOOL, LPCSTR, BOOL>(IdeFindMainThread, pattern, regexp);
}

void RunVAAutomationTestThread17(LPVOID autoRunAll)
{
	bool threadShouldWait = false;
	bool findRes = true;

	CIdeFind findTests;

	CIdeFind::CloseFindWindows();

	RunFromMainThread([&]() {
		if (gTestsActive)
		{
			SetStatus("TESTS ALREADY RUNNING.");
			return;
		}

		NotifyTestsActive(true);

		if (autoRunAll)
		{
			g_AutomatedRun = TRUE;
			Psettings->mRestrictVaToPrimaryFileTypes = false;

			// [case: 102558]
			if (gDte)
			{
				CComBSTR regRoot;
				gDte->get_RegistryRoot(&regRoot);
				CStringW regRootW = (const wchar_t*)regRoot;
				if (!regRootW.IsEmpty())
				{
					regRootW += L"\\General\\AutoRecover";
					const CString regValName("AutoRecover Enabled");
					SetRegValue(HKEY_CURRENT_USER, CString(regRootW), regValName, (DWORD)0);
				}
			}
		}
		else
		{
			g_AutomatedRun = FALSE;
		}

		gDirectiveInvokedFrom = autoRunAll ? kRunAllTestsInSolution : TestNameOnCurrentLine();
		const WTString testName = gDirectiveInvokedFrom;

		gReRun = std::make_unique<VAAutomationReRun>();

		if (!gReRun->PrepareTestsToReRun(testName.c_str(), !!g_AutomatedRun))
		{
			gReRun = nullptr; // cleanup

			findTests.SaveState();

			if (autoRunAll)
			{
				// Find All tests, and close all docs
				threadShouldWait = findRes = findTests.FindTests(EnvDTE::vsFindTargetSolution);
				PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Window.CloseAllDocuments", 0);
				PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Edit.GoToFindResults1Location", 0);
				Sleep(2000);
			}
			else if (testName == kRunAllTestsToStress)
			{
				gStressTest = true;
				threadShouldWait = findRes = findTests.FindTests(EnvDTE::vsFindTargetSolution);
			}
			else if (testName == kRunAllTestsInSolution)
				threadShouldWait = findRes = findTests.FindTests(EnvDTE::vsFindTargetSolution);
			else if (testName == kRunAllTestsInProject)
				threadShouldWait = findRes = findTests.FindTests(EnvDTE::vsFindTargetCurrentProject);
			else if (testName == RunAllTestsInOpenFiles)
				threadShouldWait = findRes = findTests.FindTests(EnvDTE::vsFindTargetOpenDocuments);
			else if (testName == kRunAllTestsInFile)
				findRes = findTests.FindTests(EnvDTE::vsFindTargetCurrentDocument);

			findTests.LoadState();
		}
	});

	if (findRes == EnvDTE::vsFindResultFound && gVaShellService && gShellAttr->IsDevenv17OrHigher())
	{
		CIdeFind::WaitFind();
	}

	if (findRes != EnvDTE::vsFindResultFound)
	{
		::RunMessageBox("Find Failed", MB_OK | MB_ICONERROR);
		RunFromMainThread([&]() { NotifyTestsActive(false); });
		return;
	}
	else
	{
		if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
			CIdeFind::ShowFindResults1();
		else
			PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "View.FindResults1", 0);

		::Sleep(500);
		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Edit.GoToFindResults1NextLocation", 0);
		::Sleep(500);
		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Window.ActivateDocumentWindow", 0);
		::Sleep(500);

		if (gShellAttr && gShellAttr->IsDevenv17OrHigher())
			CIdeFind::ShowFindResults1();
		else
			PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "View.FindResults1", 0);

		::Sleep(500);
		PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Edit.GoToFindResults1Location", 0);
		::Sleep(500);
	}

	RunFromMainThread([&]() {
		if (!autoRunAll && !sConsole)
		{
			HWND focus = ::GetFocus();
			sConsole = std::make_unique<ConsoleWnd>(_T("Visual Assist"), (short)250, (short)100);
			// leave this message on AST thread so that user can interact with IDE
			WtMessageBox("Console created.  Make sure focus is back in the IDE.", "VA Automation", MB_OK);
			if (::IsWindow(focus))
				::SetFocus(focus);
		}
		s_IdeOutputWindow = GetIdeOutputWindow("VA_AST");
		ConsoleTrace("\r\n\r\n\r\n\r\n");

		::OutputDebugString("#ASTX Starting AST thread!");
	});

	new FunctionThread(VAAutomationThread, (LPVOID)threadShouldWait, "AutoTestThread", true, true);
}

void WINAPI RunVAAutomationTest(BOOL autoRunAll /*= FALSE*/)
{
	if (gShellAttr->IsDevenv17OrHigher())
	{
		new FunctionThread(RunVAAutomationTestThread17, (LPVOID)(intptr_t)autoRunAll, "AutoTestThread17", true, true);
		return;
	}

	if (gTestsActive)
	{
		SetStatus("TESTS ALREADY RUNNING.");
		return;
	}

	NotifyTestsActive(true);

	bool threadShouldWait = false;
	bool findRes = true;

	if (autoRunAll)
	{
		g_AutomatedRun = TRUE;
		Psettings->mRestrictVaToPrimaryFileTypes = false;

		// [case: 102558]
		if (gDte)
		{
			CComBSTR regRoot;
			gDte->get_RegistryRoot(&regRoot);
			CStringW regRootW = (const wchar_t*)regRoot;
			if (!regRootW.IsEmpty())
			{
				regRootW += L"\\General\\AutoRecover";
				const CString regValName("AutoRecover Enabled");
				SetRegValue(HKEY_CURRENT_USER, CString(regRootW), regValName, (DWORD)0);
			}
		}
	}
	else
	{
		g_AutomatedRun = FALSE;
	}

	gDirectiveInvokedFrom = autoRunAll ? kRunAllTestsInSolution : TestNameOnCurrentLine();
	const WTString testName = gDirectiveInvokedFrom;

	gReRun = std::make_unique<VAAutomationReRun>();

	if (!gReRun->PrepareTestsToReRun(testName.c_str(), !!g_AutomatedRun))
	{
		gReRun = nullptr; // cleanup

		if (autoRunAll)
		{
			// Find All tests, and close all docs
			threadShouldWait = findRes = FindTests(EnvDTE::vsFindTargetSolution);
			PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Window.CloseAllDocuments", 0);
			PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM) "Edit.GoToFindResults1Location", 0);
			Sleep(2000);
		}
		else if (testName == kRunAllTestsToStress)
		{
			gStressTest = true;
			threadShouldWait = findRes = FindTests(EnvDTE::vsFindTargetSolution);
		}
		else if (testName == kRunAllTestsInSolution)
			threadShouldWait = findRes = FindTests(EnvDTE::vsFindTargetSolution);
		else if (testName == kRunAllTestsInProject)
			threadShouldWait = findRes = FindTests(EnvDTE::vsFindTargetCurrentProject);
		else if (testName == RunAllTestsInOpenFiles)
			threadShouldWait = findRes = FindTests(EnvDTE::vsFindTargetOpenDocuments);
		else if (testName == kRunAllTestsInFile)
			findRes = FindTests(EnvDTE::vsFindTargetCurrentDocument);

		if (!findRes)
		{
			::RunMessageBox("Find Failed", MB_OK | MB_ICONERROR);
			NotifyTestsActive(false);
			return;
		}
	}

	if (!autoRunAll && !sConsole)
	{
		sConsole = std::make_unique<ConsoleWnd>(_T("Visual Assist"), (short)250, (short)100);
		// leave this message on AST thread so that user can interact with IDE
		WtMessageBox("Console created.  Make sure focus is back in the IDE.", "VA Automation", MB_OK);
	}
	s_IdeOutputWindow = GetIdeOutputWindow("VA_AST");
	ConsoleTrace("\r\n\r\n\r\n\r\n");
	new FunctionThread(VAAutomationThread, (LPVOID)threadShouldWait, "AutoTestThread", true, true);
}

void VSOption::Set(LPCSTR _lang, LPCSTR _prop, LPCSTR val)
{
	this->lang = _lang;
	this->prop = _prop;
	g_IdeSettings->SetEditorOption(lang, prop, val, &orgVal);
}

VSOption::~VSOption()
{
	if (lang.IsEmpty() || prop.IsEmpty() || !g_IdeSettings)
		return;

	g_IdeSettings->SetEditorOption(lang, prop, orgVal);
}

void VSOptions::SetOption(LPCSTR lang, LPCSTR prop, LPCSTR val)
{
	results.push_back(new VSOption(lang, prop, val));
}

VSOptions::~VSOptions()
{
	RestoreOptions();
}

void VSOptions::RestoreOptions()
{
	for (std::list<VSOption*>::const_iterator it = results.begin(); it != results.end(); ++it)
		delete *it;
	results.clear();
}

bool SimulateMouseEvent::sEmulatedMouseActive = false;

BOOL Typomatic::IsOK()
{
	if (m_quit)
		return FALSE;

	const char* detected = nullptr;

	// do not change this to run from main thread
	if (g_AutomatedRun)
	{
		if (GetKeyState(VK_SHIFT) & 0x1000 && GetKeyState(VK_ESCAPE) & 0x1000)
			detected = "Shift+Escape";
		else if (!SimulateMouseEvent::sEmulatedMouseActive && GetKeyState(VK_LBUTTON) & 0x1000)
		{
			// removed due to too many false positives
			// detected = "Mouse Left Button";
		}
	}
	else if (!SimulateMouseEvent::sEmulatedMouseActive && GetKeyState(VK_LBUTTON) & 0x1000)
		detected = "Mouse Left Button";
	else if (GetKeyState(VK_ESCAPE) & 0x1000)
		detected = "Escape";

	if (detected)
	{
		WTString msg;
		msg.WTFormat("Detected event: %s.\r\n\r\n"
		             "Want to quit testing?",
		             detected);
		if (IDYES == WtMessageBox(NULL, msg, "AST", MB_YESNO))
		{
			m_quit = TRUE;
			return FALSE;
		}

		return TRUE;
	}

	HWND h = nullptr;
	DWORD fgWndProc = 0;
	int emptyTitleCount = 0;
	for (int cnt = 0; cnt < 3; ++cnt)
	{
		h = ::GetForegroundWindow();
		if (h)
		{
			if (h != m_hLastActive)
			{
				// See if this window is our proc
				::GetWindowThreadProcessId(h, &fgWndProc);
				DWORD thisProc;
				::GetWindowThreadProcessId(MainWndH, &thisProc);
				if (thisProc == fgWndProc)
					m_hLastActive = h;
			}

			if (m_hLastActive && h != m_hLastActive)
			{
				WTString txt(GetWindowTextString(h));
				if (txt.GetLength() > 60)
					txt = txt.Left(60);
				if (-1 != txt.Find("Microsoft Visual"))
				{
					// dwm takes over when VS is unresponsive
					// it also copies the window title text and displays it as its own
					// ignore when dwm is the foreground window
					// Would prefer to use GetModuleFileNameEx or similar but requires linking against psapi.dll
					// http://stackoverflow.com/questions/8475009/get-a-process-executable-name-from-process-id
					ConsoleTrace("\r\nWarning: DWM?? Change in process ID of foreground window - hwnd(%p), "
					             "process(%lu), txt(%s)\r\n",
					             h, fgWndProc, txt.c_str());
					// decrement loop count for additional (unlimited) retry
					--cnt;
				}
				else if (txt.IsEmpty() && ++emptyTitleCount < 3)
				{
					// in this case, vs is responsive again and dwm text is empty
					// again, would prefer to use psapi to definitively know that dwm is active
					ConsoleTrace("\r\nWarning: DWM (no wnd text)?? Change in process ID of foreground window - "
					             "hwnd(%p), process(%lu), txt(%s)\r\n",
					             h, fgWndProc, txt.c_str());
					// decrement loop count for additional retry (limited to 3 times via emptyTitleCount)
					--cnt;
				}
				else if (0 == txt.Find("Task Switch"))
				{
					ConsoleTrace("\r\nWarning: (Task Switcher) Change in process ID of foreground window - hwnd(%p), "
					             "process(%lu), txt(%s)\r\n",
					             h, fgWndProc, txt.c_str());
					// decrement loop count for additional (unlimited) retry
					--cnt;
				}
				else if (-1 != txt.Find("Microsoft (R) Visual"))
				{
					ConsoleTrace("\r\nWarning: (VC++ Package) Change in process ID of foreground window - hwnd(%p), "
					             "process(%lu), txt(%s)\r\n",
					             h, fgWndProc, txt.c_str());
					// decrement loop count for additional (unlimited) retry
					--cnt;
				}
				else
					ConsoleTrace(
					    "\r\nWarning: Change in process ID of foreground window - hwnd(%p), process(%lu), txt(%s)\r\n",
					    h, fgWndProc, txt.c_str());

				// check again after wait
				::Sleep(1000);
			}
			else
				return TRUE;
		}
		else
		{
			m_hLastActive = NULL;
			return TRUE;
		}
	}

	if (h && m_hLastActive && h != m_hLastActive)
	{
		WTString txt(GetWindowTextString(h));
		if (txt.GetLength() > 60)
			txt = txt.Left(60);
		ConsoleTrace("\r\nWarning: Change in process ID of foreground window - hwnd(%p), process(%lu), txt(%s)\r\n", h,
		             fgWndProc, txt.c_str());

		if (IDYES ==
		    WtMessageBox(NULL,
		                 "Detected change in process ID of foreground window.\r\n\r\n"
		                 "Want to quit testing? \r\n\r\n"
		                 "If not, change focus back to Visual Studio under test before dismissing this message.",
		                 "AST", MB_YESNO))
		{
			m_quit = TRUE;
			return FALSE;
		}

		m_hLastActive = NULL;
	}

	return TRUE;
}

bool SimulateMouseEvent::IsClickSafe()
{
	CPoint screenPos;
	::GetCursorPos(&screenPos);
	HWND wnd = ::WindowFromPoint(screenPos);

	static DWORD sVsProcessId = 0;
	if (!sVsProcessId)
	{
		::GetWindowThreadProcessId(MainWndH, &sVsProcessId);
		_ASSERTE(sVsProcessId);
	}

	DWORD wndProcId = 0;
	GetWindowThreadProcessId(wnd, &wndProcId);
	if (/*!wndProcId || */ wndProcId == sVsProcessId)
		return true;

	const char* const msg = "ERROR: prevented click in non-VS window\r\n";
	ConsoleTrace(msg);

	return false;
}

KeyboardLayoutToggle::KeyboardLayoutToggle(HKL hkl)
{
	RunFromMainThread([&]() {
		m_old_hkl = ::GetKeyboardLayout(0);

		if (hkl)
		{
			m_unload = !LayoutIsLoaded(hkl);
			m_new_hkl = LoadOrActivateLayout(hkl);
		}
		else
		{
			m_unload = false;
			m_new_hkl = m_old_hkl;
		}

		_ASSERTE(m_new_hkl == ::GetKeyboardLayout(0));
	});
}

KeyboardLayoutToggle::~KeyboardLayoutToggle()
{
	if (m_new_hkl && m_old_hkl != m_new_hkl)
	{
		RunFromMainThread([&]() {
			ActivateLayout(m_old_hkl);

			_ASSERTE(m_old_hkl == ::GetKeyboardLayout(0));

			// unload new layout
			if (m_unload)
			{
				::UnloadKeyboardLayout(m_new_hkl);
				DoEvents();
			}
		});
	}
}

void KeyPressSimulator::SendKey(SHORT vk, DWORD flags)
{
	INPUT Input;
	ZeroMemory(&Input, sizeof(Input));
	Input.type = INPUT_KEYBOARD;
	Input.ki.dwFlags = KEYEVENTF_UNICODE | flags;
	if (m_virtualKey)
		Input.ki.wVk = (WORD)vk;
	else
		Input.ki.wScan = (WORD)vk;

	for (;;)
	{
		Input.ki.time = ::GetTickCount();
		UINT keysSent = ::SendInput(1, &Input, sizeof(INPUT));
		if (keysSent != 1)
		{
			// wait and then retry once
			Sleep(250);
			keysSent = ::SendInput(1, &Input, sizeof(INPUT));
		}

		if (1 == keysSent)
			break;

		const DWORD err = ::GetLastError();
		WTString msg;
		msg.WTFormat("Failed to SendInput: vk=%d, flags=%ld, err=0x%lx", vk, flags, err);
		ConsoleTraceDirect(msg + "\r\n");

		const bool isRemote = ::GetSystemMetrics(SM_REMOTESESSION) != 0;
		// leave this message on AST thread so that user can interact with IDE
		int res = ::MessageBoxTimeoutA(
		    MainWndH, isRemote ? "SendInput failure -- lost RDC? (1)" : "SendInput failure -- lost RDC? (0)",
		    "Visual Assist AST", MB_OKCANCEL, 0, 60000);

		if (IDOK == res)
			throw WtException(msg);

		// retry
	}
}

#if defined(VA_CPPUNIT)
void RunFromMainThread(std::function<void()>, bool)
{
}
#endif
