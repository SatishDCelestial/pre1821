// ParseThrd.cpp : implementation file
//
#include "stdafxed.h"
#include "edcnt.h"
#include "ParseThrd.h"
#include "VaMessages.h"
#include "project.h"
#include "DevShellService.h"
#include "DBFile/VADBFile.h"
#include "file.h"
#include "FileTypes.h"
#include "assert_once.h"
#include "mainThread.h"
#include "VARefactor.h"
#include "Settings.h"
#include "Lock.h"
#include "DatabaseDirectoryLock.h"
#include "Armadillo\Armadillo.h"
#include "TempSettingOverride.h"
#include "fdictionary.h"
#include "VASeException/VASeException.h"
#include "VaService.h"
#include "SubClassWnd.h"
#include "CodeGraph.h"
#include "DTypeDbScope.h"
#include "FileId.h"
#include "GetFileText.h"
#include "RadStudioPlugin.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

ParserThread* g_ParserThread = NULL;
volatile long g_threadCount = 0; // number of threads running

/////////////////////////////////////////////////////////////////////////////
// ParseThrd

class FileParserWorkItem : public ParseWorkItem
{
	HWND m_hEdit;
	EdCntPtr mEd;
	CStringW m_file;
	WTString m_fileText;
	bool mIsNewProjectAddition;
	mutable bool mAbort = false;
	int m_fType;
#if defined (RAD_STUDIO)
	DWORD mStartTicks = ::GetTickCount();
	uint mSolutionHash = GlobalProject->GetSolutionHash();
#endif

  public:
	FileParserWorkItem(const CStringW& file, const WTString& text, EdCntPtr ed = NULL)
	    : ParseWorkItem("FileParserWorkItem"), m_hEdit(ed->GetSafeHwnd()), mEd(ed), m_file(file), m_fileText(text),
	      mIsNewProjectAddition(false)
	{
		vCatLog("Parser.FileParserWorkItem", "ctor");
		if (!m_file.GetLength())
			m_file = L"NewFile.h";
		m_fType = mEd ? mEd->m_ftype : GetFileType(m_file);
	}

	FileParserWorkItem(const CStringW& file, bool isNewProjectFile)
	    : ParseWorkItem("FileParserWorkItem"), m_hEdit(NULL), m_file(file), mIsNewProjectAddition(isNewProjectFile),
	      m_fType(GetFileType(m_file))
	{
		vCatLog("Parser.FileParserWorkItem", "ctor");
	}

	~FileParserWorkItem()
	{
		vCatLog("Parser.FileParserWorkItem", "dtor");
	}

	virtual bool CanRunNow() const;
	virtual void DoParseWork();
	virtual WTString GetJobName() const
	{
		return mJobName + " " + LPCTSTR(CString(m_file));
	}

	bool operator==(const FileParserWorkItem& rhs) const
	{
		if (m_hEdit == rhs.m_hEdit && mEd == rhs.mEd && m_file == rhs.m_file &&
		    mIsNewProjectAddition == rhs.mIsNewProjectAddition)
			return true;
		return false;
	}

	void Update(const FileParserWorkItem* worker)
	{
		_ASSERTE(*this == *worker);
		m_fileText = worker->m_fileText;
	}
};

bool FileParserWorkItem::CanRunNow() const
{
#if defined(RAD_STUDIO)
	// see CheckExternalFiles::CanRunNow
	if (!GlobalProject)
		return false;

	const auto kDelay = gVaRadStudioPlugin && gVaRadStudioPlugin->AreProjectsLoading() ? 10000 : 2000;
	const DWORD now = ::GetTickCount();
	if (gVaRadStudioPlugin && (!gVaRadStudioPlugin->GetProjectCount() || gVaRadStudioPlugin->AreProjectsLoading()))
	{
		// In RadStudio, we get file opens before project load
		// If no project loaded, wait before parsing files in case there is a pending project load
		if (::abs((int)(now - mStartTicks)) < kDelay)
		{
			// give chance for us to start project load.
			return false;
		}
	}

	const uint prjHash = GlobalProject->GetSolutionHash();
	if (mSolutionHash != prjHash)
	{
		if (::abs((int)(now - mStartTicks)) < kDelay)
		{
			// When VS loads solution with open files, we get edit control
			// attach before solution is loaded.
			// give chance for us to start solution load.
			return false;
		}

		if (!mSolutionHash)
		{
			// file opened before solution loaded by va
			//				_asm nop;
		}
		else if (!prjHash)
		{
			// solution unloaded; abort this call
			mAbort = true;
			return true;
		}
		else
		{
			// different non-zero hashes
			if (GlobalProject->IsBusy() || !GlobalProject->IsOkToIterateFiles())
			{
				// don't abort yet on chance that hash will change during load
				return false;
			}

			mAbort = true;
			return true;
		}
	}

	return !GlobalProject->IsBusy();
#else
	return true;
#endif
}

void FileParserWorkItem::DoParseWork()
{
	if (mAbort)
		return;

	_ASSERTE(m_file.GetLength());
#if !defined(_DEBUG)
	try
#endif // !_DEBUG
	{
		auto mp = MultiParse::Create(m_fType);
		mp->SetCacheable(TRUE);
		if (StopIt)
			return;

		const BOOL sysFile = IncludeDirs::IsSystemFile(m_file);
		DTypeDbScope removeAllDefsDbScope =
		    sysFile ?
		            // [case: 36103] fix for sys db goto defs being lost after incremental search
		            // when a sys src file has been opened for edit - hits project db so it needs to be purged too
		        ((m_hEdit && !m_fileText.IsEmpty()) ? DTypeDbScope::dbSlnAndSys : DTypeDbScope::dbSystem)
		            : DTypeDbScope::dbSolution;
		int extraDbFlag = 0;

		if (IsCFile(m_fType))
		{
			DTypePtr fd = MultiParse::GetFileData(m_file);
			if (fd)
			{
				// [case: 132428]
				// some of the symptoms of 132428 were due to RemoveAllDefs being called
				// for DTypeDbScope::dbSystem rather than DTypeDbScope::dbSlnAndSys
				if (sysFile)
				{
					if (DTypeDbScope::dbSlnAndSys == removeAllDefsDbScope)
					{
						extraDbFlag = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
					}
					else if (IncludeDirs::IsSolutionPrivateSysFile(m_file))
					{
						vLog("pt: overriding general sys as solution private sys %s", (LPCTSTR)CString(m_file));
						extraDbFlag = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
						removeAllDefsDbScope = DTypeDbScope::dbSlnAndSys;
					}
				}
				else if (DTypeDbScope::dbSystem == removeAllDefsDbScope)
				{
					if (fd->IsDbSolutionPrivateSystem())
					{
						removeAllDefsDbScope = DTypeDbScope::dbSlnAndSys;
						extraDbFlag = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
					}
					else if (fd->IsDbSolution())
					{
						removeAllDefsDbScope = DTypeDbScope::dbSlnAndSys;
						if (sysFile)
							extraDbFlag = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
					}
					else if (fd->IsDbCpp())
					{
						if (GlobalProject->Contains(m_file))
						{
							removeAllDefsDbScope = DTypeDbScope::dbSlnAndSys;
							if (sysFile)
								extraDbFlag = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
						}
					}
				}
			}
		}

		const bool kHasQueuedItems = g_ParserThread->HasQueuedItems();
		// [case: 6797] not yet ready for two pass logic - due to conditional dbouts
		// that are based on single pass removal of symbols from hash tables
		const bool kParseInSinglePass = true;
		// 			GlobalProject->IsBusy() ||
		// 			StopIt ||
		// 			!g_currentEdCnt ||
		// 			(m_hEdit && m_hEdit != g_currentEdCnt->m_hWnd);

		// load into project instead of cache if file is in project [case: 2842]
		// load into project if file is new to project (and doesn't already exist in the project) [case: 4374]
		bool parseIntoProject =
		    !sysFile && (g_pGlobDic && ((mIsNewProjectAddition || g_pGlobDic->GetFileData(m_file)) ||
		                                (GlobalProject && GlobalProject->IsBusy())));

		if (!parseIntoProject && Psettings->m_FastProjectOpen && GlobalProject)
		{
			// [case: 30226] if we don't parse all project files at load, then can't rely on GetFileData
			if (GlobalProject->Contains(m_file))
				parseIntoProject = true;
		}

		mp->RemoveAllDefs(m_file, removeAllDefsDbScope, kParseInSinglePass);

		if (StopIt)
			return;

		if (parseIntoProject)
		{
			// add an entry for the file so that VADatabase::DBIdFromParams returns DbTypeId_Solution instead of
			// DbTypeId_ExternalOther
			UINT fileId = gFileIdManager->GetFileId(m_file);
			std::shared_ptr<DbFpWriter> dbw(g_DBFiles.GetDBWriter(fileId, DbTypeId_Solution));
			if (dbw)
				dbw->DBOut(WTString("+projfile"), WTString(m_file), UNDEF, V_FILENAME | V_HIDEFROMUSER, 0, fileId);
		}

		LPCTSTR fTxt = m_fileText.IsEmpty() ? NULL : m_fileText.c_str();

		if (m_fType == Src)
			mp->ParseFileForGoToDef(m_file, !!sysFile, fTxt, extraDbFlag);
		else
			mp->FormatFile(m_file, uint(sysFile ? V_SYSLIB : V_INPROJECT), ParseType_Globals, true, fTxt,
			               (uint)extraDbFlag); // get only global stuff

		if (m_hEdit && IsWindow(m_hEdit) && mEd)
		{
			// Cannot call GetFocus in main thread
			bool sentNewDb = false;
			if (!StopIt && g_currentEdCnt == mEd)
			{
				mp->m_showRed = TRUE;
				// give clean db to EdCnt
				mp->FormatFile(m_file, V_INFILE | V_INPROJECT, ParseType_Locals, true, fTxt);
				if (m_file == mEd->FileName()) // make sure edcnt still has the same file open
				{
					if (mEd->SetNewMparse(mp, TRUE))
						sentNewDb = true;
				}
			}
			
			if (!sentNewDb && mEd->m_FileIsQuedForFullReparse)
			{
				// m_FileIsQuedForFullReparse isn't cleared if WM_VA_SET_DB isn't sent.
				// which prevents Scope from running until file is modified and AST will timeout.
				mEd->m_FileIsQuedForFullReparse = false;
			}
		}

#pragma warning(push)
#pragma warning(disable : 4127)
		// remove marked items if there are no jobs waiting
		if (!kParseInSinglePass && !kHasQueuedItems)
		{
			MultiParsePtr mp2 = MultiParse::Create(m_fType);
			if (sysFile)
				mp2->RemoveMarkedDefs((m_hEdit && !m_fileText.IsEmpty()) ? DTypeDbScope::dbSlnAndSys
				                                                         : DTypeDbScope::dbSystem);
			else
				mp2->RemoveMarkedDefs(DTypeDbScope::dbSolution);
		}
#pragma warning(pop)
	}
#if !defined(_DEBUG)
	catch (...)
	{
		VALOGEXCEPTION("FP::DPW:");
		ASSERT(FALSE);
#ifdef _DEBUG
		ErrorBox("Exception caught in FileParserWorkItem::DoParseWork");
#else
		vLog("ERROR: Exception caught in FileParserWorkItem::DoParseWork\n");
#endif
	}
#endif // !_DEBUG
}

ParserThread::ParserThread() : PooledThreadBase("ConsumerParserThread"), mIsActive(false), mActivePriority(priNone)
{
	_ASSERTE(!g_ParserThread);
	g_ParserThread = this;
	StartThread();
}

ParserThread::~ParserThread()
{
	Log("~ParserThread");

#if !defined(_DEBUG)
	try
#endif // !_DEBUG
	{
		DatabaseDirectoryLock l2;
		g_ParserThread = NULL;

		// clear queue in case we stopped before servicing all jobs - to prevent mem leak dump at exit

		while (HasQueuedItems())
		{
			ParseWorkItem* wi = GetNextJob();
#if !defined(_DEBUG)
			try
#endif // !_DEBUG
			{
				if (wi->ShouldRunAtShutdown())
					wi->DoParseWork();
				delete wi;
			}
#if !defined(_DEBUG)
			catch (...)
			{
				VALOGEXCEPTION("~PTHQ:");
			}
#endif // !_DEBUG
			mActivePriority = priNone;
		}

		while (HasLowPriorityQueuedItems())
		{
			ParseWorkItem* wi = GetNextLowPriorityJob();
#if !defined(_DEBUG)
			try
#endif // !_DEBUG
			{
				if (wi->ShouldRunAtShutdown())
					wi->DoParseWork();
				delete wi;
			}
#if !defined(_DEBUG)
			catch (...)
			{
				VALOGEXCEPTION("~PTHLPQ:");
			}
#endif // !_DEBUG
			mActivePriority = priNone;
		}
	}
#if !defined(_DEBUG)
	catch (...)
	{
		VALOGEXCEPTION("PT: dtor ex");
		_ASSERTE(!"ParserThread:: dtor exception caught!");
	}
#endif // !_DEBUG

	static bool sIsRestarting = false;
	if (!gShellIsUnloading)
	{
		VALOGEXCEPTION("PT: exit while shell running!");
		_ASSERTE(!"ParserThread:: dtor called while shell running!");

		if (!sIsRestarting)
		{
			sIsRestarting = true;
#if !defined(_DEBUG)
			try
#endif // !_DEBUG
			{
				// restart thread
				vLog("Restarting ParserThread\n");
				new ParserThread; // self deletes
			}
#if !defined(_DEBUG)
			catch (...)
			{
				VALOGEXCEPTION("~PT:");
			}
#endif // !_DEBUG
			sIsRestarting = false;
		}
	}
}

ParseWorkItem* ParserThread::GetNextJob()
{
	_ASSERTE(mActivePriority == priNone);
	mLock.Lock();
	ParseWorkItem* wi;
	if (mHighPriorityWorkItems.size() > 0)
	{
		wi = *mHighPriorityWorkItems.begin();
		mActivePriority = priHigh;
		mHighPriorityWorkItems.erase(mHighPriorityWorkItems.begin());
		mLock.Unlock();
	}
	else if (mWorkItems.size() > 0)
	{
		wi = *mWorkItems.begin();
		mActivePriority = priNormal;
		mWorkItems.erase(mWorkItems.begin());
		mLock.Unlock();

		// check mQueuedFileWorkItems and remove if in there too
		// FileParserWorkItems don't get queued to highPriorityWorkItem list
		{
			AutoLockCs l(mFileWorkItemsLock);
			for (std::vector<FileParserWorkItem*>::iterator it = mQueuedFileWorkItems.begin();
			     it != mQueuedFileWorkItems.end(); ++it)
			{
				// simple pointer compare
				if (*it == wi)
				{
					mQueuedFileWorkItems.erase(it);
					break;
				}
			}
		}
	}
	else
	{
		_ASSERTE(mBelowNormalWorkItems.size() > 0);
		wi = *mBelowNormalWorkItems.begin();
		mActivePriority = priBelowNormal;
		mBelowNormalWorkItems.erase(mBelowNormalWorkItems.begin());
		mLock.Unlock();
	}
	return wi;
}

ParseWorkItem* ParserThread::GetNextLowPriorityJob()
{
	_ASSERTE(mActivePriority == priNone);
	ParseWorkItem* wi = NULL;
	AutoLockCs l(mLock);
	if (mLowPriorityWorkItems.size() > 0)
	{
		wi = *mLowPriorityWorkItems.begin();
		mActivePriority = priLow;
		mLowPriorityWorkItems.erase(mLowPriorityWorkItems.begin());
	}
	return wi;
}

void ParserThread::QueueFile(const CStringW& file, const WTString& text /*= NULLSTR*/, EdCntPtr ed /*= NULL*/)
{
	QueueFileWorkItem(new FileParserWorkItem(file, text, ed));
}

void ParserThread::QueueNewProjectFile(const CStringW& file)
{
	QueueFileWorkItem(new FileParserWorkItem(file, true));
}

void ParserThread::QueueFileWorkItem(FileParserWorkItem* worker)
{
	{
		AutoLockCs l(mFileWorkItemsLock);
		for (std::vector<FileParserWorkItem*>::iterator it = mQueuedFileWorkItems.begin();
		     it != mQueuedFileWorkItems.end(); ++it)
		{
			// deep compare
			if (**it == *worker)
			{
				vCatLog("Parser.ParserThread", "QPWorkItem: update text %s\n", worker->GetJobName().c_str());
				// update old job with updated text
				FileParserWorkItem* oldJob = *it;
				oldJob->Update(worker);
				delete worker;
				return;
			}
		}

		mQueuedFileWorkItems.push_back(worker);
	}

	QueueParseWorkItem(worker);
}

void ParserThread::QueueParseWorkItem(ParseWorkItem* worker, WorkPriority priority /*= priNormal*/)
{
	vCatLog("Parser.ParserThread", "QPWorkItem: %s", worker->GetJobName().c_str());
	AutoLockCs l(mLock);
	switch (priority)
	{
	case priHigh:
		mHighPriorityWorkItems.push_back(worker);
		break;
	case priLow:
		mLowPriorityWorkItems.push_back(worker);
		break;
	case priBelowNormal:
		mBelowNormalWorkItems.push_back(worker);
		break;
	case priNormal:
	default:
		mWorkItems.push_back(worker);
	}
}

void ParserThread::Run()
{
	_ASSERTE(g_ParserThread == this);
	DWORD lastLowPriorityJobCompletedTickTime = 0;
	DWORD lowPriorityItemCheckTickCount = 60000;

	while (!gShellIsUnloading)
	{
		Sleep(100);
		while (!StopIt)
		{
			DWORD sleepDuration = 0;
			while (!StopIt && (!HasQueuedItems() || RefactoringActive::IsActive()))
			{
				Sleep(100);
				// [case: 18826] check for low priority items after a minute
				sleepDuration += 100;
				if (sleepDuration > lowPriorityItemCheckTickCount)
				{
					if (RefactoringActive::IsActive())
						continue;
					if (HasLowPriorityQueuedItems())
						break;
					sleepDuration = 0;
				}
			}

			// run high, normal and below normal priority jobs (not low)
			mIsActive = true;
			while (!StopIt && HasQueuedItems())
			{
				// set locks before getting next job so that high priority
				// work items added while waiting for lock get handled before
				// low priority jobs already queued
				DatabaseDirectoryLock l2;
				std::unique_ptr<ParseWorkItem> wi(GetNextJob());
				RunWorkItem(wi);
			}

			if (mSkippedItems.size())
			{
				const bool kQueuedItems = HasQueuedItems();
				// items that weren't ready before get requeued as priNormal even
				// if they were priHigh or priBelowNormal (priLow are separate)
				RequeueSkippedItems(priNormal);
				if (!kQueuedItems || (GlobalProject && GlobalProject->IsBusy()))
				{
					// [case: 61291] the sleep at the top of the loop will be skipped
					// since we just added items to the queue, so throttle here to
					// prevent cpu monopolization.
					Sleep(100);
				}
			}

			// check on low priority jobs
			const DWORD now = ::GetTickCount();
			if (((now - lastLowPriorityJobCompletedTickTime) > lowPriorityItemCheckTickCount) ||
			    (now < lastLowPriorityJobCompletedTickTime))
			{
				const DWORD kMaxLowPriorityTime = 500;
				if (HasLowPriorityQueuedItems() && (!GlobalProject || !GlobalProject->IsBusy()))
				{
					// don't spend more than kMaxLowPriorityTime ms doing low priority jobs
					while (!StopIt && HasLowPriorityQueuedItems() && (::GetTickCount() - now) < kMaxLowPriorityTime)
					{
						DatabaseDirectoryLock l2;
						std::unique_ptr<ParseWorkItem> wi(GetNextLowPriorityJob());
						if (wi.get())
							RunWorkItem(wi);
					}
				}

				lastLowPriorityJobCompletedTickTime = ::GetTickCount();
				if ((lastLowPriorityJobCompletedTickTime - now) > kMaxLowPriorityTime)
					lowPriorityItemCheckTickCount = 10000; // shorter wait if we timed out in the while loop
				else
					lowPriorityItemCheckTickCount = 60000;

				if (mSkippedItems.size())
					RequeueSkippedItems(priLow);
			}

			mIsActive = false;
			_ASSERTE(!mSkippedItems.size());
		}
	}

	vLog("PT: clean exit\n");
}

void ParserThread::RunWorkItem(std::unique_ptr<ParseWorkItem>& wi)
{
#if !defined(_DEBUG)
	try
#endif // !_DEBUG
	{
		if (wi->CanRunNow())
		{
			if (g_loggingEnabled)
			{
				vCatLog("Parser.ParserThread", "ParserThreadJob: %s\n", wi->GetJobName().c_str());
			}
			else
			{
#if defined(_DEBUG)
				WTString msg;
				msg.WTFormat("ParserThreadJob: %s\n", wi->GetJobName().c_str());
				OutputDebugString(msg.c_str());
#endif
			}

			const DWORD startTime = ::GetTickCount();
			wi->DoParseWork();
			const DWORD endTime = ::GetTickCount();
#if defined(VA_CPPUNIT) || defined(_DEBUGmem)
			_ASSERTE(_CrtCheckMemory());
#endif // VA_CPPUNIT || _DEBUGmem

			if (g_loggingEnabled)
			{
				vCatLog("Parser.ParserThread", "ParserThreadJob completed in %ld ticks\n", endTime - startTime);
			}
			else
			{
#if defined(_DEBUG)
				WTString msg;
				msg.WTFormat("ParserThreadJob completed in %ld ticks\n", endTime - startTime);
				OutputDebugString(msg.c_str());
#endif
			}
		}
		else
		{
			mSkippedItems.push_back(wi.release()); // stick back in queue and try later
		}
	}
#if !defined(_DEBUG)
	catch (...)
	{
		VALOGEXCEPTION("PT:");
		_ASSERTE(!"ParserThread::Run exception");
	}
#endif // !_DEBUG

	mActivePriority = priNone;
}

void ParserThread::RequeueSkippedItems(WorkPriority priority)
{
	AutoLockCs l(mLock);
	do
	{
		ParseWorkItem* skippedItem = *mSkippedItems.begin();
		QueueParseWorkItem(skippedItem, priority);
		mSkippedItems.erase(mSkippedItems.begin());
	} while (mSkippedItems.size());
}

bool ParserThread::HasQueuedItems() const
{
	AutoLockCs l(mLock);
	return mWorkItems.size() > 0 || mHighPriorityWorkItems.size() > 0 || mBelowNormalWorkItems.size() > 0;
}

bool ParserThread::HasLowPriorityQueuedItems() const
{
	AutoLockCs l(mLock);
	return mLowPriorityWorkItems.size() > 0;
}

bool ParserThread::IsNormalJobActiveOrPending() const
{
	AutoLockCs l(mLock);
	if (mActivePriority >= priNormal)
		return true;

	return mWorkItems.size() || mHighPriorityWorkItems.size();
}

bool ParserThread::HasBelowNormalJobActiveOrPending() const
{
	AutoLockCs l(mLock);
	if (mActivePriority == priBelowNormal)
		return true;

	return !!mBelowNormalWorkItems.size();
}

class InvalidateFileTimeCls : public ParseWorkItem
{
	CStringW mFile;

  public:
	InvalidateFileTimeCls(const CStringW& file) : ParseWorkItem("InvalidateFileTime")
	{
		mFile = file;
	}
	virtual bool ShouldRunAtShutdown() const
	{
		return true;
	}
	virtual void DoParseWork()
	{
		_ASSERTE(DatabaseDirectoryLock::GetOwningThreadID() == GetCurrentThreadId());
#if !defined(_DEBUG)
		try
#endif // !_DEBUG
		{
			g_DBFiles.InvalidateFileTime(mFile); //  To cause a reparse next time the IDE is opened
		}
#if !defined(_DEBUG)
		catch (...)
		{
			VALOGEXCEPTION("PT:InvalidateFileTime:");
			_ASSERTE(!"InvalidateFileTimeCls exception in g_DBFiles.InvalidateFileTime");
		}
#endif // !_DEBUG
	}
};

void InvalidateFileDateThread(const CStringW& file)
{
	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new InvalidateFileTimeCls(file));
}

void SynchronousFileParse(EdCntPtr ed)
{
	_ASSERTE(ed);
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	const CStringW file(ed->FileName());
	const WTString text(GetFileText(file));
	FileParserWorkItem p(file, text, ed);

	DatabaseDirectoryLock l;
	p.DoParseWork();
}
