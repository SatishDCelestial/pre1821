#pragma once

// ParseThrd.h : header file
//

#include <vector>
#include "PooledThreadBase.h"
#include "ParseWorkItem.h"
#include "DatabaseDirectoryLock.h"
#include "EdCnt_fwd.h"
#include "SpinCriticalSection.h"

// ParserThread
// ----------------------------------------------------------------------------
// Thread that queues and services parse work jobs.
// Sets the LockParseThread and DatabaseDirectoryLock locks before the
// DoParseWork method of each job is called.
//
class FileParserWorkItem;
class ParserThread : public PooledThreadBase
{
	mutable CSpinCriticalSection mLock;
	mutable CSpinCriticalSection mFileWorkItemsLock;
	std::vector<ParseWorkItem*> mWorkItems;
	std::vector<ParseWorkItem*> mBelowNormalWorkItems;
	std::vector<ParseWorkItem*> mHighPriorityWorkItems;
	std::vector<ParseWorkItem*> mLowPriorityWorkItems;
	std::vector<FileParserWorkItem*> mQueuedFileWorkItems;
	std::vector<ParseWorkItem*> mSkippedItems;
	volatile bool mIsActive;

  public:
	ParserThread();
	~ParserThread();

	void QueueFile(const CStringW& file, const WTString& text = NULLSTR, EdCntPtr ed = NULL);
	void QueueNewProjectFile(const CStringW& file);
	// lower priority items defer to higher priority items
	enum WorkPriority
	{
		priNone,

		// low pri work items are serviced intermittently and queue will not
		// be completely serviced if new higher priority items are queued
		priLow,

		// following items are serviced until none are left (unlike priLow)
		priBelowNormal,
		priNormal, // normal parse/reparse
		priHigh
	};
	void QueueParseWorkItem(ParseWorkItem* worker, WorkPriority priority = priNormal);
	bool IsActive() const
	{
		return mIsActive;
	}
	bool HasQueuedItems() const;
	bool IsNormalJobActiveOrPending() const;
	bool HasBelowNormalJobActiveOrPending() const;

  private:
	ParseWorkItem* GetNextJob();
	ParseWorkItem* GetNextLowPriorityJob();
	virtual void Run();
	void QueueFileWorkItem(FileParserWorkItem* worker);
	bool HasLowPriorityQueuedItems() const;
	void RunWorkItem(std::unique_ptr<ParseWorkItem>& wi);
	void RequeueSkippedItems(WorkPriority priority);

	WorkPriority mActivePriority;
};

extern ParserThread* g_ParserThread;
extern volatile long g_threadCount;
void InvalidateFileDateThread(const CStringW& file);
void SynchronousFileParse(EdCntPtr ed);
