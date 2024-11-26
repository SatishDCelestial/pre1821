#pragma once

class IVaShellService;
struct MarkerIds;
interface IVsTextView;
interface IVaInteropService;
interface IVsCompletionSet;
interface IVaDebuggerToolService;
interface IWpfTextView;

#ifndef __vsshell_h__
interface IVsHierarchy;
interface IVsMultiItemSelect;
interface ISelectionContainer;
typedef DWORD_PTR VSITEMID;
#endif

class IVaServiceNotifier
{
  public:
	virtual ~IVaServiceNotifier() = default;
	virtual void VaServiceShutdown() = 0;
};

class IVaService
{
  public:
	virtual ~IVaService() = default;

	// added for people that mix dll versions -
	// when VaPkg tries to call SetVaShellService, it will get DbgBrk instead
	virtual void DbgBrk_2016_05() = 0;

	// set link for VA to send commands to VS package
	virtual void SetVaShellService(IVaShellService* svc) = 0;
	// set link for VA to send commands to managed interop assembly
	virtual void SetVaInteropService(IVaInteropService* svc) = 0;
	// set link for VA to send commands to managed VS package
	virtual void SetVaManagedPackageService(
#ifdef _WIN64
	    intptr_t pIVaManagedPackageService
#else
	    int pIVaManagedPackageService
#endif
	    ) = 0;
	// other links
	virtual IVaInteropService* GetVaInteropService() = 0;
	virtual void SetVAManagedComInterop(
#ifdef _WIN64
	    intptr_t pIVAManagedComInterop
#else
	    int pIVAManagedComInterop
#endif
	    ) = 0;
	virtual IVaDebuggerToolService* GetVaDebuggerService(int managed) = 0;

	// link management
	virtual void RegisterNotifier(IVaServiceNotifier* notifier) = 0;
	virtual void UnregisterNotifier(IVaServiceNotifier* notifier) = 0;

	// toolwindow notifications from VaPkg
	virtual bool PaneCreated(HWND hWnd, LPCSTR paneName, UINT windowId) = 0;
	virtual bool PaneActivated(HWND hWnd) = 0;
	virtual void PaneDestroy(HWND hWnd) = 0;
	virtual bool PaneHandleChanged(HWND hWnd, LPCSTR paneName) = 0;

	virtual LPCWSTR GetPaneCaption(HWND hWnd) = 0;

	// va settings
	virtual bool GetBoolSetting(const char* setting) const = 0;
	virtual DWORD GetDwordSetting(const char* setting) const = 0;
	virtual char* GetStringSetting(const char* setting) const = 0; // use LocalFree on return value!
	virtual void ToggleBoolSetting(const char* setting) = 0;
	virtual void UpdateInspectionEnabledSetting(bool enabled) = 0;

	// state
	virtual DWORD GetDwordState(const char* state) const = 0;
	virtual WCHAR* GetCustomContentTypes() = 0;
	virtual void SubclassStatic(HWND hwnd) = 0;

	// targeted commands
	enum CommandTargetType
	{
		ct_none,
		ct_global,
		ct_editor,
		ct_vaview,
		ct_outline,
		ct_findRefResults,
		ct_refactor,
		ct_tracePane,
		ct_help,
		ct_hashtags
	};
	virtual DWORD QueryStatus(CommandTargetType cmdTarget, DWORD cmdId) const = 0;
	virtual HRESULT Exec(CommandTargetType cmdTarget, DWORD cmdId) = 0;
	virtual LPCWSTR QueryStatusText(CommandTargetType cmdTarget, DWORD cmdId, DWORD* statusOut) = 0;

	// document notifications from VaPkg
	virtual void DocumentModified(const WCHAR* filename, int startLineNo, int startLineIdx, int oldEndLineNo,
	                              int oldEndLineIdx, int newEndLineNo, int newEndLineIdx, int editNo) = 0;
	virtual void DocumentSaved(const WCHAR* filename) = 0;
	virtual void DocumentSavedAs(const WCHAR* newName, const WCHAR* oldName) = 0;
	virtual void DocumentClosed(const WCHAR* filename) = 0;

	// IVsTextViewEvents
	virtual DWORD SetActiveTextView(IVsTextView* textView, MarkerIds* ids) = 0;
	virtual DWORD UpdateActiveTextViewScrollInfo(IVsTextView* textView, long iBar, long iFirstVisibleUnit) = 0;
	virtual void OnSetFocus(/* [in] */ IVsTextView* pView) = 0;
	virtual void OnKillFocus(/* [in] */ IVsTextView* pView) = 0;

	// IVsTextManagerEvents
	virtual void OnUnregisterView(/* [in] */ IVsTextView* pView) = 0;

	// feature activation
	virtual bool ShouldDisplayNavBar(const WCHAR* filePath) = 0;
	virtual bool ShouldColor(const WCHAR* filePath) = 0;
	virtual bool ShouldFileBeAttachedTo(const WCHAR* filePath) = 0;
	virtual bool ShouldFileBeAttachedToForBasicServices(const WCHAR* filePath) = 0;
	virtual bool ShouldColorTooltip() = 0;
	virtual bool ShouldAttemptQuickInfoAugmentation() = 0;
	virtual bool ShouldFileBeAttachedToForSimpleHighlights(const WCHAR* filePath) = 0;

	// coloring/text
	virtual bool ColourBuildOutputLine(const wchar_t* line, int& start_colour_index, int& end_colour_index,
	                                   bool& matched) = 0;
	virtual int GetSymbolColor(void* textBuffer, LPCWSTR lineText, int linePos, int bufPos, int context) = 0;
	virtual WCHAR* GetCommentFromPos(int pos, int alreadyHasComment, int* commentFlags) = 0;
	virtual WCHAR* GetDefFromPos(int pos) = 0;
	virtual WCHAR* GetExtraDefInfoFromPos(int pos) = 0;

	// menus
	virtual void ShowSmartTagMenu(int textPos) = 0;
	virtual bool CanDisplayRefactorMenu() = 0;
	virtual bool ShouldDisplayRefactorMenuAtCursor() = 0;
	virtual class IVaMenuItemCollection* BuildRefactorMenu() = 0;

	// state changes/events
	virtual void ThemeUpdated() = 0;
	virtual bool OnChar(int ch) = 0;
	virtual void AfterOnChar(int ch) = 0;
	virtual void OnSetAggregateFocus(int pIWpfTextView_id) = 0;
	virtual void OnKillAggregateFocus(int pIWpfTextView_id) = 0;
	virtual void OnCaretVisible(int pIWpfTextView_id, bool visible) = 0;
	virtual void OnShellModal(bool isModal) = 0;
	virtual void HasVSNetQuickInfo(BOOL hasPopups) = 0;
	virtual void HasVSNetParamInfo(BOOL hasPopups) = 0;
	virtual void DgmlDoubleClick(char* nodeId) = 0;
	virtual void MoveFocusInVaView(bool goReverse) = 0;

	// completionset
	virtual bool ShowVACompletionSet(intptr_t pIvsCompletionSet) = 0;
	virtual BOOL UpdateCompletionStatus(intptr_t pCompSet, DWORD dwFlags) = 0;

	// spell check
	virtual WCHAR* GetSpellingSuggestions(char* word) = 0; // returns ;-deliminated string
	virtual void AddWordToDictionary(char* word) = 0;
	virtual void IgnoreMisspelledWord(char* word) = 0;

	// logging/testing
	virtual void WriteToLog(const WCHAR* txt) = 0;
	virtual void WriteToTestLog(const WCHAR* txt, bool menuItem = false) = 0;

	// directories
	virtual WCHAR* GetDbDir() = 0;

	// VS2017 lightweight solution load
	virtual void AsyncWorkspaceCollectionComplete(int reqId, const WCHAR* solutionFilepath,
	                                              const WCHAR* workspaceFiles) = 0;
	virtual void OnWorkspacePropertyChanged(int newState) = 0;

	virtual bool AddInclude(const WCHAR* include) = 0;
	virtual const WCHAR* GetBestFileMatch(const WCHAR* file) = 0;

	// Breakpoints debugger commands
	virtual bool OverrideNextSaveFileNameDialog(const WCHAR* filename) = 0;

	// Mouse commands
	virtual void ExecMouseCommand(int cmd, int wheel, int x, int y) = 0;

	virtual WCHAR* GetCurrentEdFilename() const = 0;
	virtual bool DelayFileOpenLineAndChar(const WCHAR* file, int ln, int cl, LPCSTR sym, BOOL preview) = 0;
	virtual bool IsFolderBasedSolution() const = 0;

	virtual int IsStepIntoSkipped(wchar_t* functionName) = 0;
	virtual bool IsProcessPerMonitorDPIAware() = 0;

	// Solution events
	virtual void ActiveProjectItemChanged(
	    IVsHierarchy* pHierOld, VSITEMID itemidOld, IVsMultiItemSelect* pMISOld, ISelectionContainer* pSCOld,
	    IVsHierarchy* pHierNew, VSITEMID itemidNew, IVsMultiItemSelect* pMISNew, ISelectionContainer* pSCNew) = 0;

	// Minihelp (VA Navbar)
	virtual DWORD GetIdealMinihelpHeight(HWND hWnd) = 0;
};


