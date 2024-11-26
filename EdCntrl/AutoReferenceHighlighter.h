#pragma once

#include "FindReferences.h"
#include "FindReferencesThread.h"
#include "EDCNT.H"
#include <memory>

class AutoReferenceHighlighter;
extern AutoReferenceHighlighter* gAutoReferenceHighlighter;

class AutoReferencesThreadObserver : public IFindReferencesThreadObserver
{
	FindReferencesThread* mFindRefsThread; // the thread owns this observer
	FindReferencesPtr mRefs;
	bool mCanceled;
	bool mHitMax;
	int mCurLine;
	enum
	{
		MaxAutoRefs = 500
	};

  public:
	AutoReferencesThreadObserver(FindReferencesPtr refs, int curln)
	    : mFindRefsThread(NULL), mRefs(refs), mCanceled(false), mHitMax(false), mCurLine(curln)
	{
		mRefs->SetObserver(this);
	}

	virtual ~AutoReferencesThreadObserver()
	{
		mRefs->SetObserver(NULL);
	}

	void SetThread(FindReferencesThread* thrd)
	{
		mFindRefsThread = thrd;
	}

	virtual void OnFoundInProject(const CStringW& /*file*/, int /*refID*/)
	{
	}
	virtual void OnFoundInFile(const CStringW& /*file*/, int /*refID*/)
	{
	}
	virtual void OnReference(int refId)
	{
		// maybe cancel after max refs found
		if (refId > MaxAutoRefs)
		{
			// check once every 100 refs above MaxAutoRefs
			if (!(refId % 100))
			{
				FindReference* ref = mRefs->GetReference((uint)refId);
				if (ref && ref->lineNo > ULONG(mCurLine + 100))
				{
					// cancel if we've gotten to curpos + x lines
					vLog("ARTO: cancel MaxAutoRefs");
					mHitMax = true;
					if (mFindRefsThread)
						mFindRefsThread->Cancel();
				}
			}
		}
	}
	virtual void OnSetProgress(int /*i*/)
	{
	}
	virtual void OnPreSearch()
	{
	}
	virtual void OnPostSearch()
	{
		// don't treat self-limiting cancel like a regular cancel
		if (mCanceled && !mHitMax)
			return;

		_ASSERTE(mRefs.get());
		_ASSERTE(g_ScreenAttrs.QueueSize_AutoReferences() == 0);
		mRefs->AddHighlightReferenceMarkers();
		SendMessage(gVaMainWnd->GetSafeHwnd(), WM_VA_THREAD_PROCESS_REF_QUEUE, 0, 0);
		_ASSERTE(g_ScreenAttrs.QueueSize_AutoReferences() == 0);
	}
	virtual void OnCancel()
	{
		mCanceled = true;
		g_ScreenAttrs.CancelAutoRefQueueProcessing();
	}
};

class AutoReferenceHighlighter
{
	friend class AutoReferencesThreadObserver;

  public:
	AutoReferenceHighlighter(EdCntPtr ed) : mAutoEd(ed), mAutoEdCookie(0), mFindRefsThread(NULL), mRefs(NULL)
	{
		try
		{
			if (mAutoEd && mAutoEd == g_currentEdCnt)
			{
				bool carriedRefForward = false;
				mEdBuf = mAutoEd->GetBuf();
				mAutoEdCookie = mAutoEd->m_modCookie;

				// this condition came from VAParseMPUnderline::ReparseScreen
				ScopeInfoPtr si = mAutoEd->ScopeInfoPtr();
				DTypePtr dat(si->GetCwData());
				if (dat && (ISCSYM(mAutoEd->WordLeftOfCursor()[0]) || ISCSYM(mAutoEd->WordRightOfCursor()[0])))
				{
					mAutoSymScope = dat->SymScope();
				}

				if (mAutoSymScope.IsEmpty() && gAutoReferenceHighlighter)
				{
					// maintain previous symscope (in whitespace, comments, unknown symbols).
					// don't check mAutoEd - so that added references are updated even
					// if caret is in whitespace of current file.
					mAutoSymScope = gAutoReferenceHighlighter->mAutoSymScope;
					carriedRefForward = true;
				}

				if (!mAutoSymScope.IsEmpty() && !gShellIsUnloading)
				{
					if (carriedRefForward && gAutoReferenceHighlighter->mAutoEd != mAutoEd)
					{
						// don't waste cycles in this file
					}
					else if (!gAutoReferenceHighlighter || gAutoReferenceHighlighter->mAutoEd != mAutoEd ||
					         gAutoReferenceHighlighter->mAutoEdCookie != mAutoEdCookie ||
					         gAutoReferenceHighlighter->mAutoSymScope != mAutoSymScope ||
					         CTer::BUF_STATE_CLEAN != mAutoEd->m_bufState ||
					         gAutoReferenceHighlighter->mEdBuf.GetLength() != mEdBuf.GetLength())
					{
						mRefs = std::make_shared<FindReferences>();
						mRefs->Init(mAutoSymScope, 0);
						delete gAutoReferenceHighlighter;
						gAutoReferenceHighlighter = this;
						// AutoReferencesThreadObserver takes ownership of mRefs
						AutoReferencesThreadObserver* obs = new AutoReferencesThreadObserver(mRefs, mAutoEd->CurLine());
						// FindRefsThread takes ownership of observer
						mFindRefsThread =
						    new FindReferencesThread(mRefs, obs, FindReferencesThread::sfOnce, "AutoReferencesThread");
						obs->SetThread(mFindRefsThread);
						return;
					}
				}
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("ARH-ctor:");
		}

		delete this;
	}

	~AutoReferenceHighlighter()
	{
		ClearThread();
	}

	int Count() const
	{
		if (mFindRefsThread && mRefs)
			return (int)mRefs->Count();
		return 0;
	}

	bool IsActive() const
	{
		if (mFindRefsThread && !mFindRefsThread->IsFinished())
			return true;
		return false;
	}

	void Cancel()
	{
		if (mFindRefsThread)
			mFindRefsThread->Cancel();
	}

	void ClearSym()
	{
		mAutoSymScope.Empty();
	}

  private:
	void ClearThread()
	{
		mRefs = NULL;
		if (!mFindRefsThread)
			return;

		FindReferencesThread* thrd = mFindRefsThread;
		mFindRefsThread = NULL;
		thrd->Cancel(TRUE);
		DeleteSpentFindReferencesThreads();

		bool doDelete = true;
		if (!thrd->IsFinished())
		{
			for (int idx = 0; idx < 5 && !thrd->IsFinished(); ++idx)
				thrd->Wait(50);

			if (!thrd->IsFinished())
			{
				// postpone thread deletion or else we risk deadlock if the thread
				// is in the middle of doing a SendMessage while the current thread
				// is waiting for that thread
				doDelete = false;
			}
		}

		if (doDelete)
			delete thrd;
		else
		{
			AutoLockCs l(gUndeletedFindThreadsLock);
			gUndeletedFindThreads.push_back(thrd);
		}
	}

  private:
	WTString mAutoSymScope;
	EdCntPtr mAutoEd;
	int mAutoEdCookie;
	WTString mEdBuf;
	FindReferencesThread* mFindRefsThread;
	FindReferencesPtr mRefs;
};

inline bool IsAutoReferenceThreadRequired()
{
	if (!Psettings->mUseMarkerApi)
		return false;

	if (Psettings->mAutoHighlightRefs && Psettings->mUseAutoHighlightRefsThread)
		return true;

	return false;
}
