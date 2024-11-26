#pragma once
#include "IVaService.h"

class CIdeFind
{
	CComBSTR prevFindWhat;
	EnvDTE::vsFindPatternSyntax prevSyntax = EnvDTE::vsFindPatternSyntax::vsFindPatternSyntaxLiteral;
	EnvDTE::vsFindResultsLocation prevLocation = EnvDTE::vsFindResults1;
	EnvDTE::vsFindTarget prevTarget = EnvDTE::vsFindTargetSolution;
	CComBSTR prevFilter;
	EnvDTE::vsFindAction prevAction = EnvDTE::vsFindActionFindAll;

  public:
	void SaveState();
	void LoadState();
	static HRESULT GetIDispatchMethods(_In_ IDispatch* pDisp, _Out_ std::map<long, std::wstring>& methodsMap);
	static void CloseFindWindows();
	static void ShowFindResults1(bool forceCreate = true, bool activate = false);
	static bool CloseAllFramesOfType(int type); // 1 for a document frame or 2 for a tool frame
	static void WaitFind(uint waitAfter = 5000, uint stuckTicksCount = 5000,
	                     std::function<bool(DWORD)> do_continue = nullptr);
	bool FindText(LPCSTR pattern, BOOL regexp);
	bool FindTests(EnvDTE::vsFindTarget fndTgt);
};

void RunVAAutomationTestThread17(LPVOID autoRunAll);
BOOL IdeFind17(LPCSTR pattern, BOOL regexp /*= FALSE*/);