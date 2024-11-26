#include "StdAfxEd.h"
#include "FindReferencesThread.h"
#include "Edcnt.h"
#include "file.h"
#include "FileTypes.h"
#include "Settings.h"
#include "VARefactor.h"
#include "DBLock.h"
#include "TraceWindowFrame.h"
#include "FeatureSupport.h"
#include "ProjectInfo.h"
#include "ParseThrd.h"
#include "DatabaseDirectoryLock.h"
#include <ppl.h>
#include "FileId.h"
#include "FDictionary.h"
#include "SubClassWnd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CCriticalSection gUndeletedFindThreadsLock;
ReferencesThreadList gUndeletedFindThreads;
typedef ReferencesThreadList::iterator ReferencesThreadListIter;
enum
{
	SoftStop = 1,
	HardStop = 2
};

void AddProject(ProjectInfoPtr prj, ProjectVec& projects);
void SortProjects(ProjectVec& projects, const CStringW& activeFile, const CStringW& matchedActiveFile);

void DeleteSpentFindReferencesThreads()
{
	AutoLockCs l(gUndeletedFindThreadsLock);
	for (ReferencesThreadListIter it = gUndeletedFindThreads.begin(); it != gUndeletedFindThreads.end();)
	{
		FindReferencesThread* thrd = *it;
		if (thrd->IsFinished())
		{
			gUndeletedFindThreads.erase(it);
			delete thrd;
			it = gUndeletedFindThreads.begin();
		}
		else
			++it;
	}
}

FindReferencesThread::FindReferencesThread(FindReferencesPtr pRefsIn, IFindReferencesThreadObserver* observer,
                                           SharedFileBehavior sfb, const char* thrdName /*= "ReferencesThread"*/)
    : PooledThreadBase(thrdName, false), mFindSymScope(pRefsIn->GetFindScope()), mObserver(observer), mStopFlag(0),
      mRefsOwnedByDlg(pRefsIn), mProgressIndex(0), mProgressMax(1), mSharedFileBehavior(sfb)
{
	_ASSERTE(mObserver);
	_ASSERTE(mSharedFileBehavior == sfOnce || Psettings->mFindRefsAlternateSharedFileBehavior);
	StartThread();
	if (!mRefsOwnedByDlg->IsAutoHighlightRef())
		mAnim = std::make_unique<StatusBarFindAnimation>();
}

FindReferencesThread::~FindReferencesThread()
{
	delete mObserver;
}

BOOL FindReferencesThread::Cancel(BOOL hardCancel /*= FALSE*/)
{
	mStopFlag = hardCancel ? HardStop : SoftStop;
	if (mAnim)
		mAnim->Off();
	mObserver->OnCancel();
	FlushAllPendingInMainThread();
	return IsRunning();
}

void FindReferencesThread::SearchFile(const CStringW& file)
{
	{
		_ASSERTE(gFileIdManager);
		const UINT fileId = gFileIdManager->GetFileId(file);

		AutoLockCs l3(mFilesSearchedLock);
		auto[it, inserted] = mFilesSearched.emplace(fileId);
		if(!inserted)
			return;
	}

	mRefsOwnedByDlg->SearchFile(mCurrentProject, file, &mStopFlag);
	if (mStopFlag)
		return;

	// autoHighlight refs get direct notify during parse
	if (mRefsOwnedByDlg->IsAutoHighlightRef())
		return;

	mRefsOwnedByDlg->FlushFileRefs(file, mObserver);
}

void FindReferencesThread::SearchProject(ProjectInfo* projInf, bool clearFilesSearched)
{
	if (clearFilesSearched)
	{
		_ASSERTE(mSharedFileBehavior == sfPerProject);
		AutoLockCs l3(mFilesSearchedLock);
		mFilesSearched.clear();
	}

	vCatLog("Parser.FindReferences", "FindReferences:Project %s (%d) (%d)", (LPCTSTR)CString(projInf->GetProjectFile()),
	     projInf->IsDeferredProject(), projInf->IsPseudoProject());
	auto cbWork = [this](const FileInfo& fi) {
		if (mStopFlag)
			return;

		const CStringW& curProjFile(fi.mFilename);
		mObserver->OnSetProgress((100 * (mProgressIndex)) / mProgressMax);
		const int ftype = ::GetFileType(curProjFile);
		if (::IsFeatureSupported(Feature_Refactoring, ftype))
			SearchFile(curProjFile);
		++mProgressIndex;
	};

	DatabaseDirectoryLock l2;
	projInf->ParallelForEachFile(cbWork);
}

void FindReferencesThread::DoSearchSolution(bool activeProjectOnly)
{
	if (GlobalProject->IsBusy() || !GlobalProject->IsOkToIterateFiles())
		return;

	mProgressIndex = 0;
	mProgressMax = GlobalProject->GetFileItemCount();
	if (!mProgressMax)
		mProgressMax = 1;

	UINT kSymFileId = mRefsOwnedByDlg->GetSymFileId();
	CStringW activeFile;
	if (g_currentEdCnt)
	{
		activeFile = g_currentEdCnt->FileName();
	}
	else
	{
		if (kSymFileId)
			activeFile = gFileIdManager->GetFile(kSymFileId);
	}

	CStringW matchedActiveFile;
	if (activeFile.GetLength())
	{
		matchedActiveFile = activeFile;
		SwapExtension(matchedActiveFile);
	}

	mObserver->OnSetProgress(0);

	RWLockReader lck;
	const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
	ProjectVec activeProjects;
	const int kProjectCnt = (int)projMap.size();
	Project::ProjectMap::const_iterator projIt;

	// locate active projects - based on active file
	if (!activeFile.IsEmpty())
		activeProjects = GlobalProject->GetProjectForFile(activeFile);

	// locate active projects - based on matched file
	if (!activeProjects.size() && matchedActiveFile.GetLength())
		activeProjects = GlobalProject->GetProjectForFile(matchedActiveFile);

	// locate active projects - based on sym file id
	if (kSymFileId)
	{
		// probe kSymFileId only if we found no activeProjects or if
		// activeProjects contains any shared or dependent projects
		bool kCheckSymFileId = !activeProjects.size();
		for (auto iter = activeProjects.begin(); !kCheckSymFileId && !mStopFlag && iter != activeProjects.end(); ++iter)
		{
			ProjectInfoPtr p = *iter;
			if (!p)
				continue;

			if (p->HasSharedProjectDependency() || p->IsSharedProjectType())
				kCheckSymFileId = true;
		}

		if (kCheckSymFileId)
		{
			ProjectVec symProjects = GlobalProject->GetProjectForFile(kSymFileId);
			if (symProjects.size())
			{
				// reverse because GetProjectForFile returns shared projects first
				for (auto iter1 = symProjects.rbegin(); iter1 != symProjects.rend(); ++iter1)
				{
					ProjectInfoPtr p1 = *iter1;
					if (!p1)
						continue;

					::AddProject(p1, activeProjects);
				}

				if (activeProjectOnly && activeProjects.size() > 1)
					mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchSharedProjects);
			}
		}
	}

	// search active and matched files first
	if (activeFile.GetLength())
	{
		if (activeProjects.size())
		{
			if (activeProjectOnly && activeProjects.size() > 1)
				mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchSharedProjects);

			// if any activeProject is shared or has a share dependency, then
			// add all shared projects to active list.
			// (the kSymFileId check does not work in the case of a symbol defined
			// in a platform project that is referenced by a shared project)
			for (auto iter = activeProjects.begin(); !mStopFlag && iter != activeProjects.end(); ++iter)
			{
				ProjectInfoPtr p = *iter;
				if (!p)
					continue;

				if (p->HasSharedProjectDependency() || p->IsSharedProjectType())
				{
					for (projIt = projMap.begin(); !mStopFlag && projIt != projMap.end(); ++projIt)
					{
						ProjectInfoPtr p2 = (*projIt).second;
						if (!p2)
							continue;

						if (p2->HasSharedProjectDependency() || p2->IsSharedProjectType())
							::AddProject(p2, activeProjects);
					}

					if (activeProjectOnly)
						mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchSharedProjects);
					break;
				}
			}

			::SortProjects(activeProjects, activeFile, matchedActiveFile);
		}
		else
		{
			DatabaseDirectoryLock l2;

			SearchFile(activeFile);
			mObserver->OnSetProgress((100 * (++mProgressIndex)) / mProgressMax);
			if (matchedActiveFile.GetLength())
			{
				SearchFile(matchedActiveFile);
				mObserver->OnSetProgress((100 * (++mProgressIndex)) / mProgressMax);
			}
			FlushAllPendingInMainThread();
		}
	}
	else if (activeProjects.empty())
	{
		// no active file?  so search whole solution
		mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchSolution);
		activeProjectOnly = false;
	}

	// start search, doing active projects first (or only if not doing whole solution)
	SearchProjectList(activeProjects, activeFile, matchedActiveFile, kProjectCnt);

	if (!activeProjectOnly)
	{
		// whole solution search
		// now search the rest of the projects
		for (projIt = projMap.begin(); !mStopFlag && projIt != projMap.end(); ++projIt)
		{
			ProjectInfoPtr projInf = (*projIt).second;
			if (!projInf)
				continue;

			if (activeProjects.size())
			{
				if (activeProjects.end() != std::find(activeProjects.begin(), activeProjects.end(), projInf))
					continue;
			}

			// if active file is from this project, search it before the rest
			// of the files in the project
			SearchActiveFilesIfFromProject(projInf, activeFile, matchedActiveFile, kProjectCnt);

			UpdateCurrentProject(kProjectCnt, projInf.get());
			// old behavior was to never clear search list
			bool clearSearchList = sfPerProject == mSharedFileBehavior;
			SearchProject(projInf.get(), clearSearchList);

			FlushAllPendingInMainThread();
		}
	}

	ClearShouldIgnoreFileCache();
}

void FindReferencesThread::SearchActiveFilesIfFromProject(ProjectInfoPtr& prj, const CStringW& activeFile,
                                                          const CStringW& matchedActiveFile, const int kProjectCnt)
{
	if ((!activeFile.IsEmpty() && prj->ContainsFile(activeFile)) ||
	    (!matchedActiveFile.IsEmpty() && prj->ContainsFile(matchedActiveFile)))
	{
		DatabaseDirectoryLock l2;
		UpdateCurrentProject(kProjectCnt, prj.get());
		SearchFile(activeFile);
		mObserver->OnSetProgress((100 * (++mProgressIndex)) / mProgressMax);
		if (!matchedActiveFile.IsEmpty())
		{
			SearchFile(matchedActiveFile);
			mObserver->OnSetProgress((100 * (++mProgressIndex)) / mProgressMax);
		}
	}
}

void FindReferencesThread::DoSearch()
{
	mCurrentProject.Empty();
	mNotifiedProject.Empty();
	{
		AutoLockCs l3(mFilesSearchedLock);
		mFilesSearched.clear();
	}

	if (mRefsOwnedByDlg->flags & FREF_Flg_InFileOnly)
	{
		mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchFile);

		CStringW fname(g_currentEdCnt->FileName());

		DatabaseDirectoryLock l2;
		SearchFile(fname);

		if (mRefsOwnedByDlg->flags & FREF_Flg_CorrespondingFile)
		{
			mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchFilePair);

			if (SwapExtension(fname) && fname.GetLength())
				SearchFile(fname);
		}
	}
	else if (mRefsOwnedByDlg->flags & FREF_Flg_FindErrors)
	{
		mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchSolution);
		SearchSolution();
	}
	else if (mRefsOwnedByDlg->flags & FREF_Flg_Reference)
	{
		BOOL isLocalOnly = g_currentEdCnt && DType::IsLocalScope(mRefsOwnedByDlg->GetFindScope());
		if (isLocalOnly) // see if it is a local var
		{
			mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchFile);
			// local variable, search only current file
			CStringW fname(g_currentEdCnt->FileName());
			DatabaseDirectoryLock l2;
			SearchFile(fname);
			if (SwapExtension(fname) && fname.GetLength())
				SearchFile(fname);
		}
		else
		{
			if (Psettings->mDisplayReferencesFromAllProjects)
			{
				mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchSolution);
				SearchSolution();
			}
			else
			{
				mRefsOwnedByDlg->SetScopeOfSearch(FindReferences::searchProject);
				SearchActiveProjects();
			}
		}
	}
	mCurrentProject.Empty();
	mNotifiedProject.Empty();
}

void FindReferencesThread::Run()
{
	// This isn't the main thread, but is a refactoring and we don't want files being reparsed or it will throw off the
	// results
	std::unique_ptr<RefactoringActive> active;
	if (!mRefsOwnedByDlg->IsAutoHighlightRef())
		active = std::make_unique<RefactoringActive>();
	TimeTrace nt(WTString("Find References ") + mFindSymScope);

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		mObserver->OnPreSearch();
		DoSearch();
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("VAFUdo:");
		Cancel();
	}
#endif // !SEAN

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		if (!gShellIsUnloading && mStopFlag != HardStop)
			mObserver->OnPostSearch();
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("VAFUpost:");
	}
#endif // !SEAN

	if (mAnim)
		mAnim->Off();
}

void FindReferencesThread::UpdateCurrentProject(int projectCnt, const ProjectInfo* projInf)
{
	FlushAllPendingInMainThread();

	// [case: 19512] to display project nodes in the results list, set mCurrentProject
	// [case: 4087] to allow select all/none behavior, do 19512 for all solutions,
	// not just those that have more than one project
	if (projectCnt /*> 1*/ && projInf)
	{
		mCurrentProject = projInf->GetProjectFile();
		if (projInf->IsDeferredProject())
			mCurrentProject += L" (deferred)";
	}
	else
	{
		mCurrentProject.Empty();
		mNotifiedProject.Empty();
	}

	_ASSERTE(!mRefsOwnedByDlg->IsAutoHighlightRef());
	if (!mCurrentProject.IsEmpty() && mCurrentProject != mNotifiedProject)
	{
		mNotifiedProject = mCurrentProject;
		const int prevCount = (int)mRefsOwnedByDlg->Count();
		mObserver->OnFoundInProject(mCurrentProject, prevCount);
	}

	FlushAllPendingInMainThread();
}

void FindReferencesThread::SearchProjectList(ProjectVec& activeProjects, const CStringW& activeFile,
                                             const CStringW& matchedActiveFile, const int kProjectCnt)
{
	for (auto iter = activeProjects.begin(); !mStopFlag && iter != activeProjects.end(); ++iter)
	{
		ProjectInfoPtr p = *iter;
		if (!p)
			continue;

		// if active file is from this project, search it before the rest
		// of the files in the project
		SearchActiveFilesIfFromProject(p, activeFile, matchedActiveFile, kProjectCnt);

		UpdateCurrentProject(kProjectCnt, p.get());
		// now search the whole project
		SearchProject(p.get(), false);

		FlushAllPendingInMainThread();
	}
}

// fix up project search order:
// shared projects, then projects that contain active file, then other projects
void SortProjects(ProjectVec& projects, const CStringW& activeFile, const CStringW& matchedActiveFile)
{
	ProjectVec tmp;
	for (auto iter = projects.begin(); iter != projects.end();)
	{
		ProjectInfoPtr p = *iter++;
		if (!p)
			continue;

		if (p->IsSharedProjectType())
		{
			projects.remove(p);
			tmp.push_back(p);
		}
		else if ((!activeFile.IsEmpty() && p->ContainsFile(activeFile)) ||
		         (!matchedActiveFile.IsEmpty() && p->ContainsFile(matchedActiveFile)))
		{
			projects.remove(p);
			tmp.push_back(p);
		}
	}

	// now add projects that don't contain the active file
	for (auto iter = projects.begin(); iter != projects.end(); ++iter)
	{
		ProjectInfoPtr p = *iter;
		if (!p)
			continue;

		tmp.push_back(p);
	}

	projects.swap(tmp);
}

void AddProject(ProjectInfoPtr prj, ProjectVec& projects)
{
	_ASSERTE(prj);

	if (std::find(projects.begin(), projects.end(), prj) != projects.end())
	{
		// already in the list
		return;
	}

	if (prj->IsSharedProjectType())
	{
		projects.push_front(prj);
	}
	else
	{
		// add to projects after any shared projects
		auto insertAt = projects.end();
		for (auto iter2 = projects.begin(); iter2 != projects.end(); ++iter2)
		{
			ProjectInfoPtr p2 = *iter2;
			if (!p2)
				continue;

			if (p2->IsSharedProjectType())
				continue;

			insertAt = iter2;
			break;
		}

		projects.insert(insertAt, prj);
	}
}
