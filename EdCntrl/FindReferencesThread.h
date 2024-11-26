#pragma once

#include <list>
#include "PooledThreadBase.h"
#include "StatusBarAnimator.h"
#include "FindReferences.h"
#include "FileList.h"
#include "project.h"
#include <memory>
#include "SpinCriticalSection.h"
#include <unordered_set>

struct ProjectInfo;

class IFindReferencesThreadObserver
{
  public:
	virtual ~IFindReferencesThreadObserver()
	{
	}
	virtual void OnFoundInProject(const CStringW& file, int refID) = 0;
	virtual void OnFoundInFile(const CStringW& file, int refID) = 0;
	virtual void OnReference(int refId) = 0;
	virtual void OnBeforeFlushReferences(size_t count) {}
	virtual void OnAfterFlushReferences() {}
	virtual void OnSetProgress(int i) = 0;
	virtual void OnPreSearch() = 0;
	virtual void OnPostSearch() = 0;
	virtual void OnCancel() = 0;
};

class FindReferencesThread : public PooledThreadBase
{
  public:
	enum SharedFileBehavior
	{
		// original behavior
		// files are restricted to appearing once in results
		// (shared files are not visible in each project, only the first project we parse)
		// preferred for rename and change signature
		sfOnce,

		// a shared file will be listed under every project that it is a part of.
		// preferred for find refs
		sfPerProject
	};

	FindReferencesThread(FindReferencesPtr pRefsIn, IFindReferencesThreadObserver* observer, SharedFileBehavior sfb,
	                     const char* thrdName = "ReferencesThread");
	~FindReferencesThread();

	BOOL Cancel(BOOL hardCancel = FALSE);
	BOOL IsStopped() const
	{
		return !!mStopFlag;
	}

	virtual void Run();

  private:
	void DoSearch();
	void SearchActiveProjects()
	{
		DoSearchSolution(true);
	}
	void SearchSolution()
	{
		DoSearchSolution(false);
	}
	void DoSearchSolution(bool activeProjectOnly);
	void SearchProjectList(ProjectVec& activeProjects, const CStringW& activeFile, const CStringW& matchedActiveFile,
	                       const int kProjectCnt);
	void SearchActiveFilesIfFromProject(ProjectInfoPtr& prj, const CStringW& activeFile,
	                                    const CStringW& matchedActiveFile, const int kProjectCnt);
	void UpdateCurrentProject(int projectCnt, const ProjectInfo* projInf);
	void SearchProject(ProjectInfo* projInf, bool clearFilesSearched);
	void SearchFile(const CStringW& file);

	std::unique_ptr<StatusBarFindAnimation> mAnim;
	WTString mFindSymScope;
	IFindReferencesThreadObserver* mObserver;
	volatile INT mStopFlag;
	FindReferencesPtr mRefsOwnedByDlg;
	CSpinCriticalSection mFilesSearchedLock;
	std::unordered_set<UINT> mFilesSearched{std::unordered_set<UINT>::size_type(32768u)};
	std::atomic_long mProgressIndex;
	int mProgressMax;
	CStringW mCurrentProject;
	CStringW mNotifiedProject;
	SharedFileBehavior mSharedFileBehavior;
};

typedef std::list<FindReferencesThread*> ReferencesThreadList;
extern ReferencesThreadList gUndeletedFindThreads;
extern CCriticalSection gUndeletedFindThreadsLock;

void DeleteSpentFindReferencesThreads();
