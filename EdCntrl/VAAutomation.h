#pragma once

#include "Typomatic.h"
#define VA_AUTOTES_STR "VAAutoTest:"
#include <vector>
#include "Settings.h"
#include "FileList.h"
#include "IVaService.h"

class VSOption
{
	CString lang, prop, orgVal;

  public:
	VSOption()
	{
	}
	VSOption(LPCSTR lang, LPCSTR prop, LPCSTR val)
	{
		Set(lang, prop, val);
	}
	~VSOption();

	void Set(LPCSTR lang, LPCSTR prop, LPCSTR val);
};

class VSOptions
{
	typedef std::list<VSOption*> VSOptionList;
	VSOptionList results;

  public:
	void SetOption(LPCSTR lang, LPCSTR prop, LPCSTR val);
	void SetOption(const WTString& lang, const WTString& prop, const WTString& val)
	{
		return SetOption(lang.c_str(), prop.c_str(), val.c_str());
	}
	void RestoreOptions();
	~VSOptions();
};

class VAAutomationTestInfo : public Typomatic
{
  protected:
	CStringW mFile;
	CStringW m_testName;
	WTString m_testCode;
	uint m_testStartPos;
	DWORD m_StartTime;
	BOOL m_InsertLineBelow;
	bool mImplicitHelpers; // do stuff without explicit commands (on by default)
	int m_TestLine;
	struct TestInfo
	{
		CStringW mTestName;
		CStringW mTestSourceFile;
		enum TestState
		{
			TestFail,
			TestPass,
			TestMissingExpectedResult
		};
		TestState mState;
		bool mLoggingEnabled;
		int mTestLine;
	};
	typedef std::vector<TestInfo> TestInfoV;
	TestInfoV mTestInfo;
	TestInfo* mCurrentTestInfo;
	bool mVsExceptionReported;

  protected:
	VAAutomationTestInfo()
	    : m_StartTime(::GetTickCount()), mImplicitHelpers(true), mCurrentTestInfo(NULL), mVsExceptionReported(false)
	{
	}
	BOOL GetTestInfo();
	bool AddTestRun();
	void GetResults(CString& duration, int& failCount, int& missingCount);
	void PokeFindResults();
	void CheckExceptionDlg(int maxWait = 1);
};

class VAAutomationOptions : public VAAutomationTestInfo
{
	CSettings m_orgSettings;
	CSettings m_defaultSettings;
	VSOptions m_VsSettings;

  protected:
	VAAutomationOptions();
	virtual ~VAAutomationOptions() = default;

	virtual void TypeString(LPCSTR code, BOOL checkOk = TRUE);
	virtual void ProcessControlCharacters(WTString controlTag);

  protected:
	UINT m_ACP; // Needs to be cleared "after" logging
};

enum SmartSelectLogging
{
	sslog_none = 0x00,  // log nothing
	sslog_block = 0x01, // log: start, end, name, text
	sslog_peek = 0x02,  // log peek markup

	sslog_default = sslog_block
};

class VAAutomationLogging : public VAAutomationOptions
{
	bool mLoggingEnabled;
	bool mLogFinalState;
	bool mMenuLoggingEnabled;
	bool mMruLoggingEnabled;
	bool mArgTooltipLoggingEnabled;
	bool mMefTooltipLoggingEnabled;
	bool mDialogLoggingEnabled;
	bool mStripLineNumbers;
	bool mSaveNewLog;
	bool mSaveAltLog;
	bool mSaveAlt2Log;
	bool mSaveAlt3Log;
	bool mSaveAlt4Log;
	CFileW m_lfile;
	DWORD
	mSmartSelectLogging;          // (OR-ed SmartSelectLogging enum values) what to log during AST when Smart Select is used
	bool mNormalSnippetExpansion; // if true, snippets are expanded without using constant/hardcoded data
	bool mNormalizeLogLinebreaks = true;
	bool mSourceLinksLoggingEnabled;
	bool mTooltipLoggingEnabled;

  public:
	bool IsMenuLoggingEnabled() const
	{
		return mMenuLoggingEnabled;
	}
	bool IsMruLoggingEnabled() const
	{
		return mMruLoggingEnabled;
	}
	bool IsArgTooltipLoggingEnabled() const
	{
		return mArgTooltipLoggingEnabled;
	}
	bool IsMefTooltipLoggingEnabled() const
	{
		return mMefTooltipLoggingEnabled;
	}
	bool IsDialogLoggingEnabled() const
	{
		return mDialogLoggingEnabled;
	}
	bool IsSourceLinksLoggingEnabled() const
	{
		return mSourceLinksLoggingEnabled;
	}
	bool IsSmartSelectLoggingPeek() const
	{
		return (mSmartSelectLogging & sslog_peek) == sslog_peek;
	}
	bool IsSmartSelectLoggingBlock() const
	{
		return (mSmartSelectLogging & sslog_block) == sslog_block;
	}
	bool StripLineNumbers() const
	{
		return mStripLineNumbers;
	}
	bool IsNormalSnippetExpansionEnabled() const
	{
		return mNormalSnippetExpansion;
	}
	bool IsTooltipLoggingEnabled() const
	{
		return mTooltipLoggingEnabled;
	}
	void LogStr(WTString str);
	void LogStrW(CStringW str);
	void TraceStr(WTString str); // so that VA can log stuff without impacting test result log
	virtual void TypeString(LPCSTR code, BOOL checkOk = TRUE);

  protected:
	VAAutomationLogging()
	    : VAAutomationOptions(), mLoggingEnabled(true), mLogFinalState(true), mMenuLoggingEnabled(false),
	      mMruLoggingEnabled(false), mArgTooltipLoggingEnabled(false), mMefTooltipLoggingEnabled(false),
	      mDialogLoggingEnabled(false), mStripLineNumbers(false), mSaveNewLog(false), mSaveAltLog(false),
	      mSaveAlt2Log(false), mSaveAlt3Log(false), mSaveAlt4Log(false), mSmartSelectLogging(sslog_default),
	      mNormalSnippetExpansion(false), mSourceLinksLoggingEnabled(false)
	{
	}
	virtual ~VAAutomationLogging() = default;

	void LogState(BOOL exactListboxContents = TRUE);
	void LogListBox(BOOL selectionState = FALSE, BOOL exactListboxContents = TRUE);
	CStringW GetLogPath(CStringW testFile, CStringW testName, BOOL BASE = FALSE);
	void CompareLogs();
	void CheckCurrentTest();
	void CloseLog();
	void LogHcb();
	void LogReferencesResults();
	void LogMarkers();
	void LogBraceMarkers();
	void LogMinihelp();
	void LogParamInfo();
	void LogQuickInfo();
	void LogScope();
	void LogSelection();
	void LogGUIDEqualityInSelection();
	void LogVaOutline();
	void LogFilesComparison(const CStringW& leftFile, const CStringW& rightFile);
	void LogVSResources(WTString category);
	void LogEditorZoom();

	void EnableLogging(bool enable)
	{
		mLoggingEnabled = enable;
		if (mCurrentTestInfo)
			mCurrentTestInfo->mLoggingEnabled = enable;
	}
	void EnableMenuLogging(bool enable)
	{
		mMenuLoggingEnabled = enable;
	}
	void EnableMruLogging(bool enable)
	{
		mMruLoggingEnabled = enable;
	}
	void EnableArgTooltipLogging(bool enable)
	{
		mArgTooltipLoggingEnabled = enable;
	}
	void EnableMefTooltipRequests(bool enable)
	{
		mMefTooltipLoggingEnabled = enable;
	}
	void EnableDialogLogging(bool enable)
	{
		mDialogLoggingEnabled = enable;
	}
	void EnableLineNumberStripping(bool enable)
	{
		mStripLineNumbers = enable;
	}
	void EnableFinalStateLogging(bool enable)
	{
		mLogFinalState = enable;
	}
	void EnableSaveOfLog(bool enable)
	{
		mSaveNewLog = enable;
	}
	void EnableSaveOfAltLog(bool enable)
	{
		mSaveAltLog = enable;
	}
	void EnableSaveOfAlt2Log(bool enable)
	{
		mSaveAlt2Log = enable;
	}
	void EnableSaveOfAlt3Log(bool enable)
	{
		mSaveAlt3Log = enable;
	}
	void EnableSaveOfAlt4Log(bool enable)
	{
		mSaveAlt4Log = enable;
	}
	void SetSmartSelectLogMask(DWORD mask)
	{
		mSmartSelectLogging = mask;
	}
	void EnableNormalSnippetExpansion(bool enable)
	{
		mNormalSnippetExpansion = enable;
	}
	void EnableLogLinebreakNormalization(bool enable)
	{
		mNormalizeLogLinebreaks = enable;
	}
	void EnableSourceLinksLogging(bool enable);
	void EnableTooltipLogging(bool enable)
	{
		mTooltipLoggingEnabled = enable;
	}

	void ResetLoggingOptions()
	{
		EnableLogging(true);
		EnableFinalStateLogging(true);
		EnableMenuLogging(false);
		EnableSaveOfLog(false);
		EnableMruLogging(false);
		EnableDialogLogging(false);
		EnableLineNumberStripping(false);
		EnableArgTooltipLogging(false);
		EnableMefTooltipRequests(false);
		SetSmartSelectLogMask(sslog_default);
		EnableNormalSnippetExpansion(false);
		EnableLogLinebreakNormalization(true);
		EnableSourceLinksLogging(false);
		EnableTooltipLogging(false);
	}

  private:
	CStringW GetLogDir(CStringW testFile);
	BOOL OpenLogFile();
};

class VAAutomationControl : public VAAutomationLogging
{
  protected:
	VAAutomationControl() : VAAutomationLogging()
	{
	}
	virtual ~VAAutomationControl() = default;

	virtual void ProcessControlCharacters(WTString controlTag);

	BOOL WaitForListBox();
	BOOL WaitForVSPopup(BOOL ignoreToolTips);
	void WaitDlg(LPCSTR caption, BOOL throwIfNotFound = TRUE);
	void CheckNoDialog(LPCSTR caption);
	void QueryStatusAndLogResult(IVaService::CommandTargetType tt, DWORD cmdId, const WTString& cmd);
};

class VAAutomation : public VAAutomationControl
{
	CStringW mFirstFileName;
	FileList mFilesTested;

  public:
	void RunTests();
	void CloseAllDocuments();
	void CloseAllWindowForStressTest(bool gotoNext);
	bool ClosePersistedUI();

  private:
	void GotoNextTest();
	bool RunTestAtCaret();
	void ReRunFailedTests();
	bool GetReRunTestInfo();
};

struct VAAutomationReRun
{
	// grab the list of tests to rerun
	struct cmpWTStringNoCase
	{
		bool operator()(const WTString& a, const WTString& b) const
		{
			return a.CompareNoCase(b.c_str()) < 0;
		}
	};
	typedef std::set<WTString> TestNameSet;
	typedef std::map<WTString, TestNameSet, cmpWTStringNoCase> TestMap;

	TestMap mFailedTests;
	TestMap mPassedTests;

#if defined(_DEBUG) || defined(VA_CPPUNIT)
	TestMap mAllInLogDbg;
#endif

	bool mPrompts = false;
	bool mAllowed = false;
	bool mRelog = true;
	bool mOnlyListed = false;
	bool mAutomat = false;
	bool mClipboard = false;
	CStringW mPath;
	WTString mSrcLog;

	CStringW mUserSettings;

	CStringW GetUserSettingStr(LPCWSTR name, const CStringW& default_value);
	bool GetUserSettingBool(LPCWSTR name, bool default_value);
	bool Init(bool isAutomatedRun);
	bool InitFromUserSettings(bool isAutomatedRun);
	bool PrepareTestsToReRun(LPCTSTR batchTest, bool isAutomatedRun);
	static size_t FindTestsInSrc(LPCTSTR in_str, bool exclude_batch_tests, bool apply_skip,
	                             std::function<void(LPCTSTR, LPCTSTR)> func);
};

void WINAPI RunVAAutomationTest(BOOL AutoRunAll = FALSE);
void ReleaseAstResources();
BOOL IdeFind(LPCSTR pattern, BOOL regexp = FALSE);

void RunDiffTool(CStringW expectedResultLog, CStringW log);

extern bool gTestsActive;
extern VAAutomationLogging* gTestLogger;
extern CStringW gTestSnippetSubDir;
extern CStringA gTestVaOptionsPage;
