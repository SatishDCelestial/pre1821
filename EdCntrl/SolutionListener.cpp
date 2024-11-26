#include "StdafxEd.h"
#include "SolutionListener.h"
#include "VaService.h"
#include "DevShellAttributes.h"
#include "VaMessages.h"
#include "PROJECT.H"
#include "FILE.H"
#include "Settings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

VsSolutionPtr gVsSolution;

VsSolutionPtr GetVsSolution()
{
	if (gVsSolution)
		return gVsSolution;

	if (!gPkgServiceProvider)
		return gVsSolution;

	_ASSERTE(g_mainThread == GetCurrentThreadId());
	IUnknown* tmp = nullptr;
	gPkgServiceProvider->QueryService(SID_SVsSolution, IID_IVsSolution, (void**)&tmp);
	if (!tmp)
		return gVsSolution;

	gVsSolution = tmp;
	return gVsSolution;
}

void ClearVsSolution()
{
	gVsSolution = nullptr;
}

SolutionListener* SolutionListener::sTimerHandler = nullptr;

SolutionListener::SolutionListener(IVaService* svc) : mVaService(svc)
{
	VsSolutionPtr pIVsSolution(GetVsSolution());
	_ASSERTE(pIVsSolution);
	if (pIVsSolution)
	{
		HRESULT res =
		    pIVsSolution->AdviseSolutionEvents(static_cast<IVsSolutionEvents*>(this), &mSolutionLoadEventsCookie);
		_ASSERTE(SUCCEEDED(res));
		std::ignore = res;
	}

	if (gShellAttr && gShellAttr->IsDevenv15OrHigher() && gPkgServiceProvider)
	{
		CComPtr<IUnknown> tmp;
		if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsSolutionBuildManager, IID_IVsSolutionBuildManager5,
		                                                (void**)&tmp)) &&
		    tmp)
		{
			{
				CComQIPtr<IVsSolutionBuildManager5> slnBuildMgr5{tmp};
				if (slnBuildMgr5)
				{
					HRESULT res = slnBuildMgr5->AdviseUpdateSolutionEvents4(
					    static_cast<IVsUpdateSolutionEvents4*>(this), &mUpdateSolutionEvents4Cookie);
					_ASSERTE(SUCCEEDED(res));
					std::ignore = res;
				}
			}

			{
				CComQIPtr<IVsSolutionBuildManager3> slnBuildMgr3{tmp};
				if (slnBuildMgr3)
				{
					HRESULT res = slnBuildMgr3->AdviseUpdateSolutionEvents3(
					    static_cast<IVsUpdateSolutionEvents3*>(this), &mUpdateSolutionEvents3Cookie);
					_ASSERTE(SUCCEEDED(res));
					std::ignore = res;
				}
			}

			{
				CComQIPtr<IVsSolutionBuildManager2> slnBuildMgr2{tmp};
				if (slnBuildMgr2)
				{
					HRESULT res = slnBuildMgr2->AdviseUpdateSolutionEvents(static_cast<IVsUpdateSolutionEvents2*>(this),
					                                                       &mUpdateSolutionEvents2Cookie);
					_ASSERTE(SUCCEEDED(res));
					std::ignore = res;
				}
			}
		}
	}

	_ASSERTE(mVaService);
	mVaService->RegisterNotifier(this);
	AddRef();

	vCatLog("Environment.Solution", "SolutionListener created\n");
	sTimerHandler = this;
	OnBeforeOpenSolution(nullptr);
	OnAfterOpenSolution(nullptr, TRUE);
}

SolutionListener::~SolutionListener()
{
	gVsSolution = nullptr;
}

// IUnknown
HRESULT STDMETHODCALLTYPE SolutionListener::QueryInterface(/* [in] */ REFIID riid,
                                                           /* [iid_is][out] */ void** ppvObject)
{
	if (riid == IID_IUnknown)
	{
		*ppvObject = this;
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IVsSolutionLoadEvents)
	{
		*ppvObject = static_cast<IVsSolutionLoadEvents*>(this);
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IVsSolutionEvents)
	{
		*ppvObject = static_cast<IVsSolutionEvents*>(this);
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IVsSolutionEvents7)
	{
		*ppvObject = static_cast<IVsSolutionEvents7*>(this);
		AddRef();
		return S_OK;
	}
	else if ((riid == IID_IVsUpdateSolutionEvents2) || (riid == IID_IVsUpdateSolutionEvents))
		{
		*ppvObject = static_cast<IVsUpdateSolutionEvents2*>(this);
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IVsUpdateSolutionEvents3)
	{
		*ppvObject = static_cast<IVsUpdateSolutionEvents3*>(this);
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IVsUpdateSolutionEvents4)
	{
		*ppvObject = static_cast<IVsUpdateSolutionEvents4*>(this);
		AddRef();
		return S_OK;
	}

	*ppvObject = nullptr;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE SolutionListener::AddRef()
{
	return (ULONG)InterlockedIncrement(&mRefCount);
}

ULONG STDMETHODCALLTYPE SolutionListener::Release()
{
	const LONG cRef = InterlockedDecrement(&mRefCount);
	if (cRef == 0)
		delete this;
	return (ULONG)cRef;
}

void SolutionListener::VaServiceShutdown()
{
	mVaService = nullptr;
	Unadvise();

	if (1 == mRefCount)
		Release();
}

void SolutionListener::Unadvise()
{
	TerminateAllTimers();

	if (mVaService)
	{
		mVaService->UnregisterNotifier(this);
		mVaService = nullptr;
	}

	if (mSolutionLoadEventsCookie)
	{
		VSCOOKIE prevCookie = mSolutionLoadEventsCookie;
		mSolutionLoadEventsCookie = 0;
		VsSolutionPtr pIVsSolution(GetVsSolution());
		if (pIVsSolution)
			pIVsSolution->UnadviseSolutionEvents(prevCookie);
	}

	if (mUpdateSolutionEvents4Cookie)
	{
		VSCOOKIE prevCookie4 = mUpdateSolutionEvents4Cookie;
		VSCOOKIE prevCookie3 = mUpdateSolutionEvents3Cookie;
		VSCOOKIE prevCookie2 = mUpdateSolutionEvents2Cookie;
		mUpdateSolutionEvents4Cookie = 0;
		mUpdateSolutionEvents3Cookie = 0;
		mUpdateSolutionEvents2Cookie = 0;

		CComPtr<IUnknown> tmp;
		if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsSolutionBuildManager, IID_IVsSolutionBuildManager5,
		                                                (void**)&tmp)) &&
		    tmp)
		{
			{
				CComQIPtr<IVsSolutionBuildManager5> slnBuildMgr{tmp};
				if (slnBuildMgr)
					slnBuildMgr->UnadviseUpdateSolutionEvents4(prevCookie4);
			}

			{
				CComQIPtr<IVsSolutionBuildManager3> slnBuildMgr{tmp};
				if (slnBuildMgr)
					slnBuildMgr->UnadviseUpdateSolutionEvents3(prevCookie3);
			}

			{
				CComQIPtr<IVsSolutionBuildManager2> slnBuildMgr{tmp};
				if (slnBuildMgr)
					slnBuildMgr->UnadviseUpdateSolutionEvents(prevCookie2);
			}
		}
	}

	sTimerHandler = nullptr;
}

// IVsSolutionLoadEvents
HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeOpenSolution(LPCOLESTR pszSolutionFilename)
{
	TerminateAllTimers();
	vCatLog("Environment.Solution", "SolutionListener::OnBeforeOpenSolution\n");
	mPreviousLoadState = mLoadState;
	mLoadState = slsLoading;
	mLoadStartTime = ::GetTickCount();
	if (GlobalProject)
		GlobalProject->SolutionLoadStarting();
	::PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_UPDATE_SOLUTION_LOAD, mLoadState, 0);
	return S_OK;
}

#if 1
//#ifndef _WIN64	// [case: 144645] guard removed ASL API
HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeBackgroundSolutionLoadBegins(void)
{
	TerminateAllTimers();
	vCatLog("Environment.Solution", "SolutionListener::OnBeforeBackgroundSolutionLoadBegins\n");
	mPreviousLoadState = mLoadState;
	mLoadState = slsBackgroundLoading;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnQueryBackgroundLoadProjectBatch(VARIANT_BOOL* pfShouldDelayLoadToNextIdle)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeLoadProjectBatch(VARIANT_BOOL fIsBackgroundIdleBatch)
{
	vCatLog("Environment.Solution", "SolutionListener::OnBeforeLoadProjectBatch ls(%d) bg(%x)\n", mLoadState, fIsBackgroundIdleBatch);
	if (slsWaitingForBackgroundLoadToStart == mLoadState && VARIANT_TRUE == fIsBackgroundIdleBatch)
	{
		// if solution has started loading before VA is loaded, and VA is now waiting for
		// background load to begin, then we can assume it started before we were loaded
		OnBeforeBackgroundSolutionLoadBegins();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterLoadProjectBatch(VARIANT_BOOL fIsBackgroundIdleBatch)
{
	// solution explorer shows transition from loading to initializing
	vCatLog("Environment.Solution", "SolutionListener::OnAfterLoadProjectBatch ls(%d) bg(%x)\n", mLoadState, fIsBackgroundIdleBatch);
	return S_OK;
}
#endif

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterBackgroundSolutionLoadComplete(void)
{
	// [case: 116501] force load in vs2017+ if slsReloading got switched to slsWaitingForBackgroundLoadToStart
	const bool forceLoad = slsReloading == mLoadState || (gShellAttr && gShellAttr->IsDevenv15OrHigher() &&
	                                                      slsReloading == mPreviousLoadState &&
	                                                      slsWaitingForBackgroundLoadToStart == mLoadState)
	                           ? true
	                           : false;
	OnAfterBackgroundSolutionLoadComplete(forceLoad);
	return S_OK;
}

void SolutionListener::OnAfterBackgroundSolutionLoadComplete(bool forceLoad)
{
	TerminateAllTimers();
	vCatLog("Environment.Solution", "SolutionListener::OnAfterBackgroundSolutionLoadComplete ls(%u) f(%d) %lu ticks\n", mLoadState, !!forceLoad,
	     ::GetTickCount() - mLoadStartTime);
	mPreviousLoadState = mLoadState;
	mLoadState = slsLoadComplete;
	if (GlobalProject)
		GlobalProject->SolutionLoadCompleted(forceLoad);
	::PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_UPDATE_SOLUTION_LOAD, mLoadState, 0);
}

// IVsSolutionEvents
HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterOpenProject(IVsHierarchy* pHierarchy, BOOL fAdded)
{
	// solution explorer shows transition from initializing to (blank or Visual Studio 2010)
	extern BOOL g_ignoreNav;
	vCatLog("Environment.Solution", "SolutionListener::OnAfterOpenProject %d\n", mLoadState);
	if (slsWaitingForBackgroundLoadToStart == mLoadState)
	{
		// this happens when mdmp is passed on command line
		// this notification fires during our startup waiting period
		// reset the WaitingForStartOfBackgroundLoad timer
		OnAfterOpenSolution(nullptr, TRUE);
		return S_OK;
	}

	if (slsLoadComplete != mLoadState && slsNone != mLoadState)
		TerminateAllTimers();

	switch (mLoadState)
	{
	case slsWaitingForBackgroundLoadToStart:
		_ASSERTE(!"this can't happen");
		vCatLog("Environment.Solution", "SolutionListener::OnAfterOpenProject slsWaitingForBackgroundLoad\n");
		return S_OK;

	case slsClosing:
		vCatLog("Environment.Solution", "SolutionListener::OnAfterOpenProject slsClosing\n");
		OnAfterCloseSolution(nullptr);
		OnBeforeOpenSolution(nullptr);
		// start timer and transition to slsReloading state
		SetWaitingToCompleteReloadTimer();
		return S_OK;

	case slsLoading:
		return S_OK;

	case slsBackgroundLoading:
		// handle as reload
	case slsReloading:
		// reset timer
		SetWaitingToCompleteReloadTimer();
		return S_OK;

	case slsLoadComplete:
	case slsNone:
		if (g_ignoreNav)
		{
			// when we do DelayFileOpen, often times that causes OnAfterOpenProject
			// to be fired; ignore so that we don't walk solution after every file open.
			// (miscellaneous files project)
		}
		else
		{
			// this block is hit in different scenarios:
			// 1. reload of project from disk after externally changed
			// 2. solution closed, lone file opened, file closed, then file opened (miscellaneous files project)
			// 3. on reload of project (after unload of project but not solution) via solution explorer context menu
			// 4. another tool (ie, resharper) queries the miscellaneous files project
			// 5. directory node opened in folder-based solution in dev15

			// for scenario 4 above, ignore misc files project changes if a solution is open
			if (pHierarchy && GlobalProject && !GlobalProject->SolutionFile().IsEmpty())
			{
				CComVariant extObj;
				HRESULT res = pHierarchy->GetProperty(VSITEMID_ROOT, VSHPROPID_ExtObject, &extObj);
				if (SUCCEEDED(res) && extObj.vt == VT_DISPATCH)
				{
					CComQIPtr<EnvDTE::Project> proj(extObj.pdispVal);
					if (proj)
					{
						CComBSTR bstr;
						proj->get_Kind(&bstr);
						if (bstr == EnvDTE::vsProjectKindMisc)
							return S_OK;

						proj->get_FullName(&bstr);
						if (bstr.Length())
						{
							CStringW proj2(bstr);
							proj2.MakeLower();
							// [case: 102228] [case: 100216]
							if (-1 != proj2.Find(L"\\singlefileisense\\_sfi_"))
								return S_OK;
						}
					}
				}
				else if (slsLoadComplete == mLoadState && GlobalProject->IsFolderBasedSolution())
				{
					// directory node was opened
					return S_OK;
				}
			}

			OnBeforeOpenSolution(nullptr);
			// start timer and transition to slsReloading state
			SetWaitingToCompleteReloadTimer();
		}
		return S_OK;

	case slsClosed:
		// new file causes new solution to be created
		OnBeforeOpenSolution(nullptr);
		OnAfterOpenSolution(nullptr, TRUE);
		return S_OK;

	default:
		_ASSERTE(!"unhandled SolutionLoadState");
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnQueryCloseProject(IVsHierarchy* pHierarchy, BOOL fRemoving,
                                                                BOOL* pfCancel)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeCloseProject(IVsHierarchy* pHierarchy, BOOL fRemoved)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterLoadProject(IVsHierarchy* pStubHierarchy,
                                                               IVsHierarchy* pRealHierarchy)
{
	KillWaitingForLoadStartTimer();
	KillWaitingForStartOfBackgroundLoadTimer();
	// do not kill reload timer
	vCatLog("Environment.Solution", "SolutionListener::OnAfterLoadProject %d\n", mLoadState);
	if (slsWaitingForBackgroundLoadToStart == mLoadState)
	{
		mPreviousLoadState = mLoadState;
		mLoadState = slsLoading;
	}
	_ASSERTE(slsLoading == mLoadState || slsBackgroundLoading == mLoadState || slsReloading == mLoadState);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnQueryUnloadProject(IVsHierarchy* pRealHierarchy, BOOL* pfCancel)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeUnloadProject(IVsHierarchy* pRealHierarchy,
                                                                  IVsHierarchy* pStubHierarchy)
{
	vCatLog("Environment.Solution", "SolutionListener::OnBeforeUnloadProject %d\n", mLoadState);
	if (slsClosing != mLoadState)
	{
		_ASSERTE(slsWaitingForBackgroundLoadToStart == mLoadState || slsLoading == mLoadState ||
		         slsReloading == mLoadState || slsLoadComplete == mLoadState);
		OnBeforeOpenSolution(nullptr);
		// start timer and transition to slsReloading state
		SetWaitingToCompleteReloadTimer();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterOpenSolution(IUnknown* pUnkReserved, BOOL fNewSolution)
{
	TerminateAllTimers();
	vCatLog("Environment.Solution", "SolutionListener::OnAfterOpenSolution %d\n", mLoadState);

	switch (mLoadState)
	{
	case slsClosed:
		// opening file after close of solution
		OnBeforeOpenSolution(nullptr);
		break;
	case slsLoadComplete:
		// after startup with no auto-load, creating new file cause OnAfterOpen to fire without OnBeforeOpen
		OnBeforeOpenSolution(nullptr);
		break;
	case slsReloading:
	case slsLoading:
	case slsNone:
	case slsWaitingForBackgroundLoadToStart:
		break;
	default:
		_ASSERTE(!"unexpected solution load state in OnAfterOpenSolution");
	}

	auto fn = [](int loadState) -> uint {
		static bool first = true;
		if (first)
		{
			first = false;
			return 5000u;
		}

		switch (loadState)
		{
		case slsNone:
			return 5000u;
		default:
			return 2000u;
		}
	};

	mPreviousLoadState = mLoadState;
	const uint kTimerVal = fn(mLoadState);
	mLoadState = slsWaitingForBackgroundLoadToStart;
	AddRef();
	mWaitingForBackgroundLoadStartTimerId =
	    ::SetTimer(nullptr, 0, kTimerVal, (TIMERPROC)&WaitingForStartOfBackgroundLoadTimerProc);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnQueryCloseSolution(IUnknown* pUnkReserved, BOOL* pfCancel)
{
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeCloseSolution(IUnknown* pUnkReserved)
{
	TerminateAllTimers();
	vCatLog("Environment.Solution", "SolutionListener::OnBeforeCloseSolution\n");
	mPreviousLoadState = mLoadState;
	mLoadState = slsClosing;
	if (GlobalProject)
		GlobalProject->SolutionClosing();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterCloseSolution(IUnknown* /*pUnkReserved*/)
{
	TerminateAllTimers();
	vCatLog("Environment.Solution", "SolutionListener::OnAfterCloseSolution\n");
	mPreviousLoadState = mLoadState;
	mLoadState = slsClosed;
	::PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_UPDATE_SOLUTION_LOAD, mLoadState, 0);
	AddRef();
	mPostCloseWaitingForLoadTimerId = ::SetTimer(nullptr, 0, 500, (TIMERPROC)&WaitingForLoadStartTimerProc);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterOpenFolder(LPCOLESTR folderPath)
{
	// [case: 101740]
	OnBeforeOpenSolution(nullptr);
	// no background load
	// [case: 116692] force load in folder mode due to switching of git branches
	OnAfterBackgroundSolutionLoadComplete(true);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeCloseFolder(LPCOLESTR folderPath)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnQueryCloseFolder(LPCOLESTR folderPath, BOOL* pfCancel)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterCloseFolder(LPCOLESTR folderPath)
{
	if (slsClosed != mLoadState)
		OnAfterCloseSolution(nullptr);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterLoadAllDeferredProjects(void)
{
	// [case: 105291]
	// by the time we get this, we have already gotten OnAfterOpenProject
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_Begin(BOOL* pfCancelUpdate)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_Done(BOOL fSucceeded, BOOL fModified, BOOL fCancelCommand)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_StartUpdate(BOOL* pfCancelUpdate)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_Cancel()
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnActiveProjectCfgChange(IVsHierarchy* pIVsHierarchy)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateProjectCfg_Begin(IVsHierarchy* pHierProj, IVsCfg* pCfgProj,
                                                                   IVsCfg* pCfgSln, DWORD dwAction, BOOL* pfCancel)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateProjectCfg_Done(IVsHierarchy* pHierProj, IVsCfg* pCfgProj,
                                                                  IVsCfg* pCfgSln, DWORD dwAction, BOOL fSuccess,
                                                                  BOOL fCancel)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnBeforeActiveSolutionCfgChange(IVsCfg* pOldActiveSlnCfg,
                                                                            IVsCfg* pNewActiveSlnCfg)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnAfterActiveSolutionCfgChange(IVsCfg* pOldActiveSlnCfg,
                                                                           IVsCfg* pNewActiveSlnCfg)
{
	if (mLoadState == slsLoadComplete && !Psettings->mForceUseOldVcProjectApi && gShellAttr &&
	    gShellAttr->IsDevenv15u3OrHigher() && GlobalProject && GlobalProject->IsVcFastProjectLoadEnabled())
	{
		// [case: 108577]
		// force a reload when configuration changes since FPL can break
		// interrogation of inactive configurations
		OnBeforeOpenSolution(nullptr);
		mPreviousLoadState = mLoadState;
		mLoadState = slsReloading;
		// start timer and transition to slsReloading state
		SetWaitingToCompleteReloadTimer();
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_QueryDelayFirstUpdateAction(int* pfDelay)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_BeginFirstUpdateAction()
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_EndLastUpdateAction()
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_BeginUpdateAction(DWORD dwAction)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::UpdateSolution_EndUpdateAction(DWORD dwAction)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnActiveProjectCfgChangeBatchBegin()
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE SolutionListener::OnActiveProjectCfgChangeBatchEnd()
{
	return S_OK;
}

void CALLBACK SolutionListener::WaitingForStartOfBackgroundLoadTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	KillTimer(hWnd, idEvent);

	SolutionListener* tmp = sTimerHandler;
	if (tmp)
		tmp->WaitingForStartOfBackgroundLoadTimerFired();
}

void SolutionListener::WaitingForStartOfBackgroundLoadTimerFired()
{
	vCatLog("Environment.Solution", "SolutionListener::WaitingForStartOfBackgroundLoadTimerFired\n");
	if (!mWaitingForBackgroundLoadStartTimerId)
		return;

	mWaitingForBackgroundLoadStartTimerId = 0;
	if (slsWaitingForBackgroundLoadToStart == mLoadState)
	{
		// assume that there is no background load
		OnAfterBackgroundSolutionLoadComplete();
	}

	Release();
}

void SolutionListener::KillWaitingForStartOfBackgroundLoadTimer()
{
	if (mWaitingForBackgroundLoadStartTimerId && sTimerHandler)
	{
		::KillTimer(nullptr, mWaitingForBackgroundLoadStartTimerId);
		mWaitingForBackgroundLoadStartTimerId = 0;
		// since the timer didn't run, do the release() that it would have done
		Release();
	}
}

void CALLBACK SolutionListener::WaitingForLoadStartTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	KillTimer(hWnd, idEvent);

	SolutionListener* tmp = sTimerHandler;
	if (tmp)
		tmp->WaitingForLoadStartTimerFired();
}

void SolutionListener::WaitingForLoadStartTimerFired()
{
	vCatLog("Environment.Solution", "SolutionListener::WaitingForLoadStartTimerFired\n");
	if (!mPostCloseWaitingForLoadTimerId)
		return;

	mPostCloseWaitingForLoadTimerId = 0;
	if (slsClosed == mLoadState)
	{
		// load defaults
		if (GlobalProject)
			GlobalProject->SolutionLoadCompleted(false);
	}

	Release();
}

void SolutionListener::KillWaitingForLoadStartTimer()
{
	if (mPostCloseWaitingForLoadTimerId && sTimerHandler)
	{
		::KillTimer(nullptr, mPostCloseWaitingForLoadTimerId);
		mPostCloseWaitingForLoadTimerId = 0;
		// since the timer didn't run, do the release() that it would have done
		Release();
	}
}

void SolutionListener::SetWaitingToCompleteReloadTimer()
{
	_ASSERTE(!mWaitingToCompleteReloadTimerId);
	auto fn = [](int loadState) -> uint {
		switch (loadState)
		{
		case slsReloading:
		case slsBackgroundLoading:
			return 4000u;
		default:
			return 2000u;
		}
	};
	const uint kTimerElapseVal = fn(mLoadState);
	if (slsReloading != mLoadState)
	{
		mPreviousLoadState = mLoadState;
		mLoadState = slsReloading;
	}
	AddRef();
	mWaitingToCompleteReloadTimerId =
	    ::SetTimer(nullptr, 0, kTimerElapseVal, (TIMERPROC)&WaitingToCompleteReloadTimerProc);
}

void CALLBACK SolutionListener::WaitingToCompleteReloadTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
	KillTimer(hWnd, idEvent);

	SolutionListener* tmp = sTimerHandler;
	if (tmp)
		tmp->WaitingToCompleteReloadTimerFired();
}

void SolutionListener::WaitingToCompleteReloadTimerFired()
{
	vCatLog("Environment.Solution", "SolutionListener::WaitingToCompleteReloadTimerFired\n");
	if (!mWaitingToCompleteReloadTimerId)
		return;

	mWaitingToCompleteReloadTimerId = 0;
	if (slsReloading == mLoadState)
	{
		if (gVaService && gVaService->IsShellModal())
		{
			// modal prompt for reload is up - reset timer and wait more
			SetWaitingToCompleteReloadTimer();
		}
		else
		{
			// assume that reload is complete
			OnAfterBackgroundSolutionLoadComplete();
		}
	}
	else
	{
		vCatLog("Environment.Solution", "SolutionListener no action taken\n");
	}

	Release();
}

void SolutionListener::KillWaitingToCompleteReloadTimer()
{
	if (mWaitingToCompleteReloadTimerId && sTimerHandler)
	{
		::KillTimer(nullptr, mWaitingToCompleteReloadTimerId);
		mWaitingToCompleteReloadTimerId = 0;
		// since the timer didn't run, do the release() that it would have done
		Release();
	}
}

void SolutionListener::TerminateAllTimers()
{
	KillWaitingToCompleteReloadTimer();
	KillWaitingForStartOfBackgroundLoadTimer();
	KillWaitingForLoadStartTimer();
}

/*
vs2010 transitions:
    -load-
        SL::OnBeforeOpenSol
        SL::OnAfterOpenSol
        ::CheckSol
        ::LoadSol2
    -unload-
        SL::OnBeforeCloseSol
        SL::OnAfterCloseSol
        ::CheckSol
        ::LoadSol2
    -load-
        SL::OnBeforeOpenSol
        SL::OnAfterOpenSol
        ::CheckSol
        ::LoadSol2
    -load another without explicit unload-
        SL::OnBeforeCloseSol
        SL::OnAfterCloseSol
        SL::OnBeforeOpenSol
        SL::OnAfterOpenSol
        ::CheckSol
        ::LoadSol2
    -exit-
        SL::OnBeforeCloseSol
        SL::OnAfterCloseSol

dev11 transitions:
    -load-
        SL::OnBeforeOpenSol
        SL::OnAfterOpenSol
        SL::OnBeforeBackgroundSolutionLoadBegins
        SL::OnAfterBackgroundSolutionLoadComplete
    -unload-
        SL::OnBeforeCloseSol
        SL::OnAfterCloseSol
        ::CheckSol
        ::LoadSol2
    -load-
        SL::OnBeforeOpenSol
        SL::OnAfterOpenSol
        SL::OnBeforeBackgroundSolutionLoadBegins
        ::CheckSol
        ::LoadSol2
        SL::OnAfterBackgroundSolutionLoadComplete
    -load another without explicit unload-
        SL::OnBeforeCloseSol
        SL::OnAfterCloseSol
        SL::OnBeforeOpenSol
        SL::OnAfterOpenSol
        SL::OnBeforeBackgroundSolutionLoadBegins
        ::CheckSol
        ::LoadSol2
        SL::OnAfterBackgroundSolutionLoadComplete
    -exit-
        SL::OnBeforeCloseSol
        SL::OnAfterCloseSol

*/
