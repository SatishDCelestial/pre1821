#pragma once

struct tagSettings;

// for calls from the VS addin dlls (VAssistNet.dll and VAssist.dll) to VA_X.dll
__interface IVaVsAddinClient
{
	// old extern C functions of VA_X.dll
	BOOL SetupVa(IUnknown * serviceProviderUnk);
	void SetMainWnd(HWND wnd);
	tagSettings* GetSettings();
	void SettingsUpdated(DWORD option);
	void Shutdown();
	HWND AttachEditControl(HWND hMSDevEdit, const WCHAR* file, void* pDoc);
	LRESULT PreDefWindowProc(UINT, WPARAM, LPARAM);
	LRESULT PostDefWindowProc(UINT, WPARAM, LPARAM);

	// vs2002+ support (VAssistNet.dll)
	BOOL CheckSolution();
	BOOL LoadSolution();
	void AddFileToProject(const WCHAR* pProject, const WCHAR* pfile, BOOL nonbinarySourceFile);
	void RemoveFileFromProject(const WCHAR* pProject, const WCHAR* pfile, BOOL nonbinarySourceFile);
	void RenameFileInProject(const WCHAR* pProject, const WCHAR* pOldfilename, const WCHAR* pNewfilename);
	int GetTypingDevLang();
	void ExecutingDteCommand(int execing, LPCSTR command);

	// vc6 support (VAssist.dll)
	void LoadWorkspace(const WCHAR* projFiles);
	void CloseWorkspace();
	void SaveBookmark(LPCWSTR filename, int lineNo, BOOL clearAllPrevious);
};
