#pragma once

#include "IVaService.h"
#include "vsshell100.h"
#include "vsshell110.h"
#include "vsshell150.h"
#include "SolutionLoadState.h"

class SolutionListener : public IVsSolutionEvents,
                         public IVsSolutionEvents7,
                         public IVsSolutionLoadEvents,
                         public IVsUpdateSolutionEvents2,
                         public IVsUpdateSolutionEvents3,
                         public IVsUpdateSolutionEvents4,
                         public IVaServiceNotifier
{
  public:
	SolutionListener(IVaService* svc);
	~SolutionListener();

  private:
	// IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
	    /* [in] */ REFIID riid,
	    /* [iid_is][out] */ void** ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();

	// IVsSolutionLoadEvents
	virtual HRESULT STDMETHODCALLTYPE OnBeforeOpenSolution(LPCOLESTR pszSolutionFilename);
#if 1
	//#ifndef _WIN64
	virtual HRESULT STDMETHODCALLTYPE OnBeforeBackgroundSolutionLoadBegins();
	virtual HRESULT STDMETHODCALLTYPE OnQueryBackgroundLoadProjectBatch(VARIANT_BOOL* pfShouldDelayLoadToNextIdle);
	virtual HRESULT STDMETHODCALLTYPE OnBeforeLoadProjectBatch(VARIANT_BOOL fIsBackgroundIdleBatch);
	virtual HRESULT STDMETHODCALLTYPE OnAfterLoadProjectBatch(VARIANT_BOOL fIsBackgroundIdleBatch);
#elif defined(_WIN64) && defined(VA_CPPUNIT)
	virtual HRESULT STDMETHODCALLTYPE OnBeforeBackgroundSolutionLoadBegins()
	{
		return E_FAIL;
	}
	virtual HRESULT STDMETHODCALLTYPE OnQueryBackgroundLoadProjectBatch(VARIANT_BOOL*)
	{
		return E_FAIL;
	}
	virtual HRESULT STDMETHODCALLTYPE OnBeforeLoadProjectBatch(VARIANT_BOOL)
	{
		return E_FAIL;
	}
	virtual HRESULT STDMETHODCALLTYPE OnAfterLoadProjectBatch(VARIANT_BOOL)
	{
		return E_FAIL;
	}
#endif
	virtual HRESULT STDMETHODCALLTYPE OnAfterBackgroundSolutionLoadComplete();
	void OnAfterBackgroundSolutionLoadComplete(bool forceLoad);

	// IVsSolutionEvents
	virtual HRESULT STDMETHODCALLTYPE OnAfterOpenProject(IVsHierarchy* pHierarchy, BOOL fAdded);
	virtual HRESULT STDMETHODCALLTYPE OnQueryCloseProject(IVsHierarchy* pHierarchy, BOOL fRemoving, BOOL* pfCancel);
	virtual HRESULT STDMETHODCALLTYPE OnBeforeCloseProject(IVsHierarchy* pHierarchy, BOOL fRemoved);
	virtual HRESULT STDMETHODCALLTYPE OnAfterLoadProject(IVsHierarchy* pStubHierarchy, IVsHierarchy* pRealHierarchy);
	virtual HRESULT STDMETHODCALLTYPE OnQueryUnloadProject(IVsHierarchy* pRealHierarchy, BOOL* pfCancel);
	virtual HRESULT STDMETHODCALLTYPE OnBeforeUnloadProject(IVsHierarchy* pRealHierarchy, IVsHierarchy* pStubHierarchy);
	virtual HRESULT STDMETHODCALLTYPE OnAfterOpenSolution(IUnknown* pUnkReserved, BOOL fNewSolution);
	virtual HRESULT STDMETHODCALLTYPE OnQueryCloseSolution(IUnknown* pUnkReserved, BOOL* pfCancel);
	virtual HRESULT STDMETHODCALLTYPE OnBeforeCloseSolution(IUnknown* pUnkReserved);
	virtual HRESULT STDMETHODCALLTYPE OnAfterCloseSolution(IUnknown* pUnkReserved);

	// IVsSolutionEvents7
	virtual HRESULT STDMETHODCALLTYPE OnAfterOpenFolder(LPCOLESTR folderPath);
	virtual HRESULT STDMETHODCALLTYPE OnBeforeCloseFolder(LPCOLESTR folderPath);
	virtual HRESULT STDMETHODCALLTYPE OnQueryCloseFolder(LPCOLESTR folderPath, BOOL* pfCancel);
	virtual HRESULT STDMETHODCALLTYPE OnAfterCloseFolder(LPCOLESTR folderPath);
	virtual HRESULT STDMETHODCALLTYPE OnAfterLoadAllDeferredProjects(void);

	// IVsUpdateSolutionEvents
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_Begin(BOOL* pfCancelUpdate);
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_Done(BOOL fSucceeded, BOOL fModified, BOOL fCancelCommand);
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_StartUpdate(BOOL* pfCancelUpdate);
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_Cancel();
	virtual HRESULT STDMETHODCALLTYPE OnActiveProjectCfgChange(IVsHierarchy* pIVsHierarchy);

	// IVsUpdateSolutionEvents2
	virtual HRESULT STDMETHODCALLTYPE UpdateProjectCfg_Begin(IVsHierarchy* pHierProj, IVsCfg* pCfgProj, IVsCfg* pCfgSln,
	                                                         DWORD dwAction, BOOL* pfCancel);
	virtual HRESULT STDMETHODCALLTYPE UpdateProjectCfg_Done(IVsHierarchy* pHierProj, IVsCfg* pCfgProj, IVsCfg* pCfgSln,
	                                                        DWORD dwAction, BOOL fSuccess, BOOL fCancel);

	// IVsUpdateSolutionEvents3
	virtual HRESULT STDMETHODCALLTYPE OnBeforeActiveSolutionCfgChange(IVsCfg* pOldActiveSlnCfg,
	                                                                  IVsCfg* pNewActiveSlnCfg);
	virtual HRESULT STDMETHODCALLTYPE OnAfterActiveSolutionCfgChange(IVsCfg* pOldActiveSlnCfg,
	                                                                 IVsCfg* pNewActiveSlnCfg);

	// IVsUpdateSolutionEvents4
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_QueryDelayFirstUpdateAction(int* pfDelay);
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_BeginFirstUpdateAction();
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_EndLastUpdateAction();
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_BeginUpdateAction(DWORD dwAction);
	virtual HRESULT STDMETHODCALLTYPE UpdateSolution_EndUpdateAction(DWORD dwAction);
	virtual HRESULT STDMETHODCALLTYPE OnActiveProjectCfgChangeBatchBegin();
	virtual HRESULT STDMETHODCALLTYPE OnActiveProjectCfgChangeBatchEnd();

	// IVaServiceNotifier
	virtual void VaServiceShutdown();

	void Unadvise();
	void TerminateAllTimers();

	// timer used to complete load in case background load doesn't start
	// used during slsWaitingForBackgroundLoadToStart load state
	static void CALLBACK WaitingForStartOfBackgroundLoadTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD);
	void WaitingForStartOfBackgroundLoadTimerFired();
	void KillWaitingForStartOfBackgroundLoadTimer();

	// timer used to wait for load of next solution after another solution was closed
	// used during slsClosed load state
	static void CALLBACK WaitingForLoadStartTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD);
	void WaitingForLoadStartTimerFired();
	void KillWaitingForLoadStartTimer();

	// timer used to complete load since reload has no completed notification
	// used during slsReloading load state
	void SetWaitingToCompleteReloadTimer();
	static void CALLBACK WaitingToCompleteReloadTimerProc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD);
	void WaitingToCompleteReloadTimerFired();
	void KillWaitingToCompleteReloadTimer();

  private:
	IVaService* mVaService;
	VSCOOKIE mSolutionLoadEventsCookie = 0;
	VSCOOKIE mUpdateSolutionEvents2Cookie = 0;
	VSCOOKIE mUpdateSolutionEvents3Cookie = 0;
	VSCOOKIE mUpdateSolutionEvents4Cookie = 0;
	LONG mRefCount = 0;
	SolutionLoadState mLoadState = slsNone;
	SolutionLoadState mPreviousLoadState = slsNone;
	UINT_PTR mWaitingForBackgroundLoadStartTimerId = 0;
	UINT_PTR mPostCloseWaitingForLoadTimerId = 0;
	UINT_PTR mWaitingToCompleteReloadTimerId = 0;
	DWORD mLoadStartTime = 0;
	static SolutionListener* sTimerHandler;
};

using VsSolutionPtr = CComQIPtr<IVsSolution>;
VsSolutionPtr GetVsSolution();
void ClearVsSolution();
