#pragma once

#include "IVaService.h"
#include "IVaShellService.h"
#include "../VaMef/VAX.Interop/IVaInteropService.h"
#include <map>
#include <list>
#include "WTString.h"
#include "VSIP\8.0\VisualStudioIntegration\Common\Inc\textmgr.h"
#ifdef _WIN64
#include "..\VaManagedComLib\VaManagedComLib64_h.h"
#else
#include "..\VaManagedComLib\VaManagedComLib_h.h"
#endif

class LiveOutlineFrame;
class FindReferencesResultsFrame;
class TraceWindowFrame;
class VaHashtagsFrame;
class FindReferences;
interface IVaManagedComService;
interface IServiceProvider;
interface IVsRunningDocumentTable;
class FileVersionInfo;

class VaService : public IVaService
{
  public:
	VaService();
	virtual ~VaService();

	// Interface IVaService
	virtual void RegisterNotifier(IVaServiceNotifier* notifier);
	virtual void UnregisterNotifier(IVaServiceNotifier* notifier);
	virtual bool PaneCreated(HWND hWnd, LPCSTR paneName, UINT windowId);
	virtual bool PaneHandleChanged(HWND hWnd, LPCSTR paneName);
	virtual bool PaneActivated(HWND hWnd);
	virtual void PaneDestroy(HWND hWnd);
	virtual LPCWSTR GetPaneCaption(HWND hWnd);
	virtual bool GetBoolSetting(const char* setting) const;
	virtual DWORD GetDwordSetting(const char* setting) const;
	virtual char* GetStringSetting(const char* setting) const; // use LocalFree on return value!
	virtual void ToggleBoolSetting(const char* setting);
	virtual void UpdateInspectionEnabledSetting(bool enabled) override;
	virtual DWORD GetDwordState(const char* state) const;
	virtual DWORD QueryStatus(CommandTargetType cmdTarget, DWORD cmdId) const;
	virtual HRESULT Exec(CommandTargetType cmdTarget, DWORD cmdId);
	virtual LPCWSTR QueryStatusText(CommandTargetType cmdTarget, DWORD cmdId, DWORD* statusOut);
	virtual void SetVaShellService(IVaShellService* svc);

	virtual void SetVaInteropService(IVaInteropService* Svc);
	virtual void SetVaManagedPackageService(
#ifdef _WIN64
	    intptr_t pIVaManagedPackageService
#else
	    int pIVaManagedPackageService
#endif
	);
	virtual void DocumentModified(const WCHAR* filename, int startLineNo, int startLineIdx, int oldEndLineNo,
	                              int oldEndLineIdx, int newEndLineNo, int newEndLineIdx, int editNo);
	virtual void DocumentSaved(const WCHAR* filename);
	virtual void DocumentSavedAs(const WCHAR* newName, const WCHAR* oldName);
	virtual void DocumentClosed(const WCHAR* filename);
	virtual DWORD SetActiveTextView(IVsTextView* textView, MarkerIds* ids);
	virtual DWORD UpdateActiveTextViewScrollInfo(IVsTextView* textView, long iBar, long iFirstVisibleUnit);
	virtual void OnSetFocus(/* [in] */ IVsTextView* pView);
	virtual void OnKillFocus(/* [in] */ IVsTextView* pView);
	virtual void OnUnregisterView(/* [in] */ IVsTextView* pView);
	virtual bool ShouldDisplayNavBar(const WCHAR* filePath);
	virtual bool ShouldColor(const WCHAR* filePath);
	virtual bool ShouldFileBeAttachedTo(const WCHAR* filePath);
	virtual bool ShouldFileBeAttachedToForBasicServices(const WCHAR* filePath);
	virtual bool ShouldFileBeAttachedToForSimpleHighlights(const WCHAR* filePath);
	virtual WCHAR* GetCustomContentTypes();
	virtual bool ShouldColorTooltip();
	virtual bool ShouldAttemptQuickInfoAugmentation();
	virtual int GetSymbolColor(void* textBuffer, LPCWSTR lineText, int linePos, int bufPos, int context);
	virtual bool ShowVACompletionSet(intptr_t pIvsCompletionSet);
	virtual WCHAR* GetCommentFromPos(int pos, int alreadyHasComment, int* commentFlags);
	virtual WCHAR* GetDefFromPos(int pos);
	virtual WCHAR* GetExtraDefInfoFromPos(int pos);
	virtual bool OnChar(int ch);
	virtual void AfterOnChar(int ch);
	virtual void OnSetAggregateFocus(int pIWpfTextView_id);
	virtual void OnKillAggregateFocus(int pIWpfTextView_id);
	virtual void OnCaretVisible(int pIWpfTextView_id, bool visible);
	virtual void OnShellModal(bool isModal);
	virtual void HasVSNetQuickInfo(BOOL hasPopups);
	virtual void HasVSNetParamInfo(BOOL hasPopups);
	virtual void ShowSmartTagMenu(int textPos);
	virtual void SetVAManagedComInterop(
#ifdef _WIN64
	    intptr_t pIVAManagedComInterop
#else
	    int pIVAManagedComInterop
#endif
	);
	virtual IVaInteropService* GetVaInteropService();
	virtual WCHAR* GetSpellingSuggestions(char* word);
	virtual void AddWordToDictionary(char* word);
	virtual void IgnoreMisspelledWord(char* word);
	virtual void DgmlDoubleClick(char* nodeId);
	virtual void WriteToLog(const WCHAR* txt);
	virtual void WriteToTestLog(const WCHAR* txt, bool menuItem = false);
	virtual bool ColourBuildOutputLine(const wchar_t* line, int& start_colour_index, int& end_colour_index,
	                                   bool& matched);
	virtual void ThemeUpdated();
	virtual void SubclassStatic(HWND hwnd);
	virtual void MoveFocusInVaView(bool goReverse);
	virtual BOOL UpdateCompletionStatus(intptr_t pCompSet, DWORD dwFlags);
	virtual IVaDebuggerToolService* GetVaDebuggerService(int managed) override;
	virtual bool CanDisplayRefactorMenu() override;
	virtual bool ShouldDisplayRefactorMenuAtCursor() override;
	virtual IVaMenuItemCollection* BuildRefactorMenu() override;
	virtual WCHAR* GetDbDir() override;
	virtual void AsyncWorkspaceCollectionComplete(int reqId, const WCHAR* solutionFilepath,
	                                              const WCHAR* workspaceFiles) override;
	virtual void OnWorkspacePropertyChanged(int newState);

	virtual bool AddInclude(const WCHAR* include);
	virtual const WCHAR* GetBestFileMatch(const WCHAR* file);

	virtual bool OverrideNextSaveFileNameDialog(const WCHAR* filename);

	virtual void ExecMouseCommand(int cmd, int wheel, int x, int y);
	virtual WCHAR* GetCurrentEdFilename() const override;
	virtual bool DelayFileOpenLineAndChar(const WCHAR* file, int ln, int cl, LPCSTR sym, BOOL preview) override;
	virtual bool IsFolderBasedSolution() const override;

	virtual int IsStepIntoSkipped(wchar_t* functionName);
	virtual void DbgBrk_2016_05();

	virtual bool IsProcessPerMonitorDPIAware() override;

	LiveOutlineFrame* GetOutlineFrame() const
	{
		return mOutlineFrame;
	}
	TraceWindowFrame* GetTraceFrame() const
	{
		return mTraceFrame;
	}
	VaHashtagsFrame* GetHashtagsFrame() const
	{
		return mHashtagsFrame;
	}
	void FindReferencesFrameCreated(HWND refWnd, FindReferencesResultsFrame* frm);
	void FindReferences(int flags, int typeImageIdx, WTString symScope);
	void DisplayReferences(const ::FindReferences& refs);
	bool IsFindReferencesRunning();

	bool IsShellModal() const
	{
		return mShellIsModal;
	}
	void AbortInit()
	{
		mInitAborted = true;
	}
	void VsServicesReady();

	void ActiveProjectItemChanged(
		IVsHierarchy* pHierOld, VSITEMID itemidOld, IVsMultiItemSelect* pMISOld, ISelectionContainer* pSCOld, 
		IVsHierarchy* pHierNew, VSITEMID itemidNew, IVsMultiItemSelect* pMISNew, ISelectionContainer* pSCNew) override;

	DWORD GetIdealMinihelpHeight(HWND hWnd) override;

 private:
	static void CALLBACK FindReferencesCallback(HWND hWnd = NULL, UINT ignore1 = 0, UINT_PTR idEvent = 0,
	                                            DWORD ignore2 = 0);
	static void StartFindReferences(int flags, int typeImageIdx, WTString symScope);
	void FindReferencesPaneCreated(HWND hWnd);
	void FindReferencesPaneActivated(HWND hWnd);
	bool FindReferencesPaneDestroyed(HWND hWnd);
	void FindReferencesClonePaneCreated(HWND hWnd, UINT windowId);
	void VaViewPaneCreated(HWND hWnd);
	void VaViewActivated() const;
	void VaViewDestroyed();
	void LiveOutlineCreated(HWND hWnd);
	void LiveOutlineActivated() const;
	void LiveOutlineDestroyed();
	void HashtagsPaneCreated(HWND hWnd);
	void HashtagsPaneActivated() const;
	void HashtagsPaneDestroyed();
	void TracePaneCreated(HWND hWnd);
	void TracePaneActivated() const;
	void TracePaneDestroyed();
	HRESULT ExecDynamicSelectionCommand(DWORD cmdId);

	enum WindowPaneType
	{
		wptEmpty = 0,
		wptVaView,
		wptVaOutline,
		wptFindRefResults,
		wptFindRefResultsClone,
		wptTraceWindow,
		wptHashtags
	};

	WindowPaneType GetWindowPaneType(LPCSTR paneName);

	typedef std::map<HWND, WindowPaneType> PaneMap;
	PaneMap mPanes;

	typedef std::map<HWND, FindReferencesResultsFrame*> FindReferencesWndMap;
	FindReferencesWndMap mFindRefPanes;

	typedef std::list<IVaServiceNotifier*> NotifierList;
	typedef NotifierList::iterator NotifierIter;
	NotifierList mNotifiers;

	LiveOutlineFrame* mOutlineFrame;
	TraceWindowFrame* mTraceFrame;
	VaHashtagsFrame* mHashtagsFrame;

	CString mWhatIsNewUrl;

	bool mShellIsModal;
	bool mInitAborted;

	// [case: 141731]
	struct FindReferenceParametersStruct
	{
		int flags = 0;
		int typeImageIdx = 0;
		WTString symScope;
	};
	static FindReferenceParametersStruct sFindReferenceParameters;
};

extern VaService* gVaService;
extern IVaShellService* gVaShellService;
extern IVaInteropService* gVaInteropService;
extern CComQIPtr<IServiceProvider> gPkgServiceProvider;
extern CComQIPtr<IVsTextManager> gVsTextManager;
extern CComQIPtr<IVsHiddenTextManager> gVsHiddenTextManager;
extern CComPtr<IVsRunningDocumentTable> gVsRunningDocumentTable;
extern CComQIPtr<IVaManagedPackageService> g_vaManagedPackageSvc;
extern BOOL gHasMefParamInfo;
extern BOOL gHasMefQuickInfo;
extern bool gTypingAllowed;
extern LONG gExecActive;
extern DWORD gCurrExecCmd;
extern FileVersionInfo* gIdeVersion;