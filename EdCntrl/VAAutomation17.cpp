#include "stdafxed.h"
#include "VAAutomation17.h"
#include "VAAutomation.h"
#include "PROJECT.H"
#include "VaService.h"

void CIdeFind::SaveState()
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());

	CComPtr<EnvDTE::Find> iFinder;
	if (SUCCEEDED(gDte->get_Find(&iFinder)))
	{
		HRESULT res;
		res = iFinder->get_FindWhat(&prevFindWhat);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->get_PatternSyntax(&prevSyntax);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->get_ResultsLocation(&prevLocation);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->get_Target(&prevTarget);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->get_FilesOfType(&prevFilter);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->get_Action(&prevAction);
		_ASSERTE(SUCCEEDED(res));
	}
}

void CIdeFind::LoadState()
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());

	CComPtr<EnvDTE::Find> iFinder;
	if (SUCCEEDED(gDte->get_Find(&iFinder)))
	{
		HRESULT res;
		res = iFinder->put_FindWhat(prevFindWhat);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_PatternSyntax(prevSyntax);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_ResultsLocation(prevLocation);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_Target(prevTarget);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_FilesOfType(prevFilter);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_Action(prevAction);
		_ASSERTE(SUCCEEDED(res));
	}
}

HRESULT CIdeFind::GetIDispatchMethods(_In_ IDispatch* pDisp, _Out_ std::map<long, std::wstring>& methodsMap)
{
	HRESULT hr = S_OK;

	CComPtr<IDispatch> spDisp(pDisp);
	if (!spDisp)
		return E_INVALIDARG;

	CComPtr<ITypeInfo> spTypeInfo;
	hr = spDisp->GetTypeInfo(0, 0, &spTypeInfo);
	if (SUCCEEDED(hr) && spTypeInfo)
	{
		TYPEATTR* pTatt = nullptr;
		hr = spTypeInfo->GetTypeAttr(&pTatt);
		if (SUCCEEDED(hr) && pTatt)
		{
			FUNCDESC* fd = nullptr;
			for (uint i = 0; i < (uint)pTatt->cFuncs; ++i)
			{
				hr = spTypeInfo->GetFuncDesc(i, &fd);
				if (SUCCEEDED(hr) && fd)
				{
					CComBSTR funcName;
					spTypeInfo->GetDocumentation(fd->memid, &funcName, nullptr, nullptr, nullptr);
					if (funcName.Length() > 0)
					{
						methodsMap[fd->memid] = funcName;
					}

					spTypeInfo->ReleaseFuncDesc(fd);
				}
			}

			spTypeInfo->ReleaseTypeAttr(pTatt);
		}
	}

	return hr;
}

void CIdeFind::CloseFindWindows()
{
	RunFromMainThread([]() {
		CComPtr<IVsUIShell> uishell;
		if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&uishell)) && uishell)
		{
			CComPtr<IEnumWindowFrames> pEnum;
			if (SUCCEEDED(uishell->GetToolWindowEnum(&pEnum)))
			{
				CComPtr<IVsWindowFrame> pFrame;
				ULONG fetched = 0;
				while (SUCCEEDED(pEnum->Next(1, &pFrame, &fetched)) && fetched > 0)
				{
					CComVariant varCapt;
					if (SUCCEEDED(pFrame->GetProperty(VSFPROPID_Caption, &varCapt)) && varCapt.bstrVal)
					{
						CStringW caption(varCapt.bstrVal);
						if (caption.Find(L"Find ") == 0)
						{
							pFrame->CloseFrame(FRAMECLOSE_NoSave);
						}
					}

					pFrame.Release();
				}
			}
		}
	});
}

void CIdeFind::ShowFindResults1(bool forceCreate /*= true*/, bool activate /*= false*/)
{
	RunFromMainThread([=]() {
		CComPtr<IVsUIShell> uishell;
		if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&uishell)) && uishell)
		{
			GUID wndKindFindRslt1;
			CStringW wstr(EnvDTE::vsWindowKindFindResults1);
			CComPtr<IVsWindowFrame> pFrame;
			if (SUCCEEDED(IIDFromString(wstr, &wndKindFindRslt1)) &&
			    SUCCEEDED(uishell->FindToolWindow(forceCreate ? FTW_fForceCreate : FTW_fFindFirst, wndKindFindRslt1,
			                                      &pFrame)))
			{
				if (!gWtMessageBoxCount)
				{
					if (activate)
						pFrame->Show();
					else
						pFrame->ShowNoActivate();
				}
			}
		}
	});
}

bool CIdeFind::CloseAllFramesOfType(int type)
{
	bool succeeded = true;

	RunFromMainThread([type, &succeeded]() {
		CComPtr<IVsUIShell> uishell;
		if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&uishell)) && uishell)
		{
			CComPtr<IEnumWindowFrames> pFrameEnum;
			if (SUCCEEDED(uishell->GetToolWindowEnum(&pFrameEnum)))
			{
				CComPtr<IVsWindowFrame> pFrame;
				ULONG fetched = 0;
				if (SUCCEEDED(pFrameEnum->Next(1, &pFrame, &fetched)) && fetched == 1)
				{
					CComVariant frameType;
					if (SUCCEEDED(pFrame->GetProperty(VSFPROPID_Type, &frameType)) && frameType.intVal == type)
					{
						if (!SUCCEEDED(pFrame->CloseFrame(FRAMECLOSE_NoSave)))
							succeeded = false;
					}
				}
			}
		}
	});

	return succeeded;
}

void CIdeFind::WaitFind(uint waitAfter /*= 5000*/, uint stuckTicksCount /*= 5000*/,
                        std::function<bool(DWORD)> do_continue /*= nullptr*/)
{
	_ASSERTE(g_mainThread != GetCurrentThreadId());

	DWORD waitingTicks = 0;
	bool waiting = true;

	ULONGLONG stuckTicks = 0;
	CString stuckTxt;

	DWORD startTicks = GetTickCount();

	auto update_status = [&]() {
		if (do_continue && !do_continue(GetTickCount() - startTicks))
		{
			waiting = false;
			return;
		}

		if (!waitingTicks)
		{
			CString currentTxt;
			currentTxt = gVaShellService->GetStatusText();

			if (currentTxt.Compare(stuckTxt) == 0)
			{
				if (!stuckTicks)
				{
					stuckTicks = GetTickCount();
				}
				else if (GetTickCount() - stuckTicks > stuckTicksCount)
				{
					waiting = false;
					return;
				}
			}
			else
			{
				stuckTicks = 0;
				stuckTxt = currentTxt;
			}

			waiting = currentTxt.Find("Matching lines:") == -1;

			if (!waiting && waitAfter)
			{
				waitingTicks = GetTickCount();
				waiting = true;
			}
		}
		else
		{
			waiting = GetTickCount() - waitingTicks < waitAfter;
		}
	};

	RunFromMainThread(update_status);

	while (waiting)
	{
		Sleep(0);
		RunFromMainThread(update_status);

		// 			if (GetTickCount64() - startTicks > 60000)
		// 				break;
	}
}

bool CIdeFind::FindText(LPCSTR pattern, BOOL regexp)
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());

	EnvDTE::vsFindResult findRes = EnvDTE::vsFindResultError;

	CComPtr<EnvDTE::Find> iFinder;
	if (SUCCEEDED(gDte->get_Find(&iFinder)))
	{
		HRESULT res;
		// do find
		res = iFinder->put_FindWhat(CComBSTR(pattern));
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_ResultsLocation(EnvDTE::vsFindResultsNone);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_Target(EnvDTE::vsFindTargetCurrentDocument);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_PatternSyntax(regexp ? EnvDTE::vsFindPatternSyntaxRegExpr
		                                        : EnvDTE::vsFindPatternSyntaxLiteral);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_MatchCase(FALSE);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_Backwards(FALSE);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_MatchWholeWord(FALSE);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_FilesOfType(CComBSTR(L"*.*"));
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_Action(EnvDTE::vsFindActionFind);
		_ASSERTE(SUCCEEDED(res));

		res = iFinder->Execute(&findRes);
		_ASSERTE(SUCCEEDED(res));
	}

	return findRes == EnvDTE::vsFindResultFound;
}

bool CIdeFind::FindTests(EnvDTE::vsFindTarget fndTgt)
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());

	EnvDTE::vsFindResult findRes = EnvDTE::vsFindResultError;

	CComPtr<EnvDTE::Find> iFinder;
	if (SUCCEEDED(gDte->get_Find(&iFinder)))
	{
		HRESULT res;
		res = iFinder->put_FindWhat(CComBSTR(VA_AUTOTES_STR));
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_PatternSyntax(EnvDTE::vsFindPatternSyntaxLiteral);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_ResultsLocation(EnvDTE::vsFindResults1);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_Target(fndTgt);
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_FilesOfType(CComBSTR(L"*.*"));
		_ASSERTE(SUCCEEDED(res));
		res = iFinder->put_Action(EnvDTE::vsFindActionFindAll);
		_ASSERTE(SUCCEEDED(res));

		res = iFinder->Execute(&findRes);
		_ASSERTE(SUCCEEDED(res));
	}

	return findRes == EnvDTE::vsFindResultFound;
}

BOOL IdeFind17(LPCSTR pattern, BOOL regexp /*= FALSE*/)
{
	// [case: 61671] if file is modified, find can return success but not
	// actually work. give dte a chance to sync.
	Sleep(250);

	bool result = false;
	RunFromMainThread([&]() {
		CIdeFind find;
		find.SaveState();
		result = find.FindText(pattern, regexp);
		::Sleep(500);
		find.LoadState();
	});

	return result ? TRUE : FALSE;
}
