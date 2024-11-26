#include "stdafxed.h"
#include "FindReferencesResultsFrame.h"
#include "FindUsageDlg.h"
#include "FindReferencesThread.h"
#include "DevShellService.h"
#include "DevShellAttributes.h"
#include "Edcnt.h"
#include "WindowUtils.h"
#include "VaService.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "project.h"

#ifdef RAD_STUDIO
#include "CppBuilder.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// gActiveFindRefsResultsFrame is just one of the many possible results windows
// that may be in existence.  There is one primary results window.
// There can be many secondary results windows that are static copies of
// the primary results.
FindReferencesResultsFrame* gActiveFindRefsResultsFrame = NULL;
static bool IsVc6MdiWnd(HWND wnd);

// FindReferencesResultsFrame
// ----------------------------------------------------------------------------
//
FindReferencesResultsFrame::FindReferencesResultsFrame() : mResultsWnd(NULL)
{
}

FindReferencesResultsFrame::~FindReferencesResultsFrame()
{
	if (mResultsWnd)
	{
		if (IsThreadRunning())
		{
			mResultsWnd->OnCancel();
			::Sleep(500);
		}

#ifdef RAD_STUDIO
		if (mResultsWnd->m_hWnd)
			mResultsWnd->DestroyWindow();
#else
		if (IsWindow(mResultsWnd->m_hWnd))
			mResultsWnd->SendMessage(WM_DESTROY);
#endif

		delete mResultsWnd;
		mResultsWnd = NULL;
	}
}

bool FindReferencesResultsFrame::CommonCtor(HWND hResultsPane)
{
	if (gVaService)
		gVaService->FindReferencesFrameCreated(hResultsPane, this);

	// Create FindUsage window
	mResultsWnd->Create(FindUsageDlg::IDD, this);
	mResultsWnd->SetParent(this);

#ifdef RAD_STUDIO
	RS_ResizeParentsToChild(mResultsWnd->m_hWnd, true);
#else
	OnSize();
#endif
	mResultsWnd->Show(SW_NORMAL);
	return true;
}

LRESULT
FindReferencesResultsFrame::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (WM_ERASEBKGND == message)
	{
		if (gShellAttr && gShellAttr->IsDevenv11OrHigher() && wParam)
		{
			// [case: 74848]
			HDC hdc = (HDC)wParam;
			CRect rect;
			GetClientRect(&rect);
			CBrush br;
			if (br.CreateSolidBrush(CVS2010Colours::GetVS2010Colour(VSCOLOR_ENVIRONMENT_BACKGROUND_GRADIENTBEGIN)))
				::FillRect(hdc, rect, br);
			return 1;
		}
	}

	LRESULT r = CWnd::WindowProc(message, wParam, lParam);
	if (WM_TIMER != message && WM_DESTROY != message && mResultsWnd && mResultsWnd->GetSafeHwnd())
	{
		switch (message)
		{
		case WM_COMMAND:
			if (wParam == WM_SIZE || wParam == WM_MOVE)
				OnSize(); // not called in vc6, used by vs?
			break;
		case WM_SIZE:
			OnSize();
			break;
		case WM_SETFOCUS:
			gActiveFindRefsResultsFrame = this;
			mResultsWnd->m_treeSubClass.SetFocus();
			break;
		}
	}
	return r;
}

void FindReferencesResultsFrame::OnSize()
{
	CRect rc;
	GetClientRect(&rc);
	mResultsWnd->MoveWindow(&rc);
}

DWORD
FindReferencesResultsFrame::GetFoundUsageCount() const
{
	return (uint)mResultsWnd->mRefs->Count();
}

void FindReferencesResultsFrame::FocusFoundUsage()
{
	gActiveFindRefsResultsFrame = this;
	mResultsWnd->m_treeSubClass.SetFocus();
}

DWORD
FindReferencesResultsFrame::QueryStatus(DWORD cmdId) const
{
	switch (cmdId)
	{
	case icmdVaCmd_FindReferences:
	case icmdVaCmd_FindReferencesInFile:
		return 1u;
	case icmdVaCmd_RefResultsNext:
	case icmdVaCmd_RefResultsPrev:
		return mResultsWnd ? 1u : 0u;
	case icmdVaCmd_RefResultsFind:
	case icmdVaCmd_RefResultsFindNext:
	case icmdVaCmd_RefResultsFindPrev:
		return 1u; // case=9191 enable always, check for real on exec
	case icmdVaCmd_RefResultsClone:
		return IsOkToClone() ? 1u : 0u;
	case icmdVaCmd_RefResultsGoto:
	case icmdVaCmd_RefResultsDelete:
	case icmdVaCmd_RefResultsCopy:
	case icmdVaCmd_RefResultsCopyAll:
	case icmdVaCmd_RefResultsCut:
		return mResultsWnd /*&& mResultsWnd->m_tree.GetSelectedItem()*/ ? 1u : 0u;
	case icmdVaCmd_RefResultsClearAll:
		return GetFoundUsageCount() ? 1u : 0u;
	case icmdVaCmd_RefResultsStop:
		if (mResultsWnd && GetReferencesThread() && !GetReferencesThread()->IsFinished())
		{
			return 1u;
		}
		return 0u;
	case icmdVaCmd_RefResultsRefresh:
		if (mResultsWnd && (!GetReferencesThread() || (GetReferencesThread() && GetReferencesThread()->IsFinished())) &&
		    mResultsWnd->mRefs->GetFindSym().GetLength() && mResultsWnd->mRefs->GetFindScope().GetLength())
		{
			return 1u;
		}
		return 0u;
	case icmdVaCmd_RefResultsToggleHighlight:
	case icmdVaCmd_RefResultsContextMenu:
		return mResultsWnd ? 1u : 0u;
	case icmdVaCmd_RefResultsSort:
	case icmdVaCmd_RefResultsExpand:
	case icmdVaCmd_RefResultsCollapse:
	case icmdVaCmd_RefResultsToggleTooltips:
		// these are not enabled in the ctc file
		return 0u;
		//		return 1 | (someBool ? 0x80000000 : 0); // latching command
	}

	return (DWORD)-2;
}

HRESULT
FindReferencesResultsFrame::Exec(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_RefResultsClone:
		_ASSERTE(!"icmdVaCmd_RefResultsClone should not be handled in FindReferencesResultsFrame::Exec");
		break;
	case icmdVaCmd_RefResultsNext:
		if (mResultsWnd && GetFoundUsageCount())
			mResultsWnd->GotoNextItem();
		break;
	case icmdVaCmd_RefResultsPrev:
		if (mResultsWnd && GetFoundUsageCount())
			mResultsWnd->GotoPreviousItem();
		break;
	case icmdVaCmd_RefResultsFind:
		// always enabled - check state on exec
		if (mResultsWnd && (!GetReferencesThread() || (GetReferencesThread() && GetReferencesThread()->IsFinished())) &&
		    GetFoundUsageCount())
		{
			mResultsWnd->OnFind();
		}
		break;
	case icmdVaCmd_RefResultsFindNext:
	case icmdVaCmd_RefResultsFindPrev:
		// always enabled - check state on exec
		if (GetFoundUsageCount() && mResultsWnd && mResultsWnd->HasFindText())
		{
			if (icmdVaCmd_RefResultsFindNext == cmdId)
				mResultsWnd->OnFindNext();
			else if (icmdVaCmd_RefResultsFindPrev == cmdId)
				mResultsWnd->OnFindPrev();
		}
		break;
	case icmdVaCmd_RefResultsGoto:
		mResultsWnd->GoToSelectedItem();
		break;
	case icmdVaCmd_RefResultsDelete:
		mResultsWnd->RemoveSelectedItem();
		break;
	case icmdVaCmd_RefResultsClearAll:
		mResultsWnd->RemoveAllItems();
		break;
	case icmdVaCmd_RefResultsStop:
		mResultsWnd->OnCancel();
		break;
	case icmdVaCmd_RefResultsRefresh:
		mResultsWnd->OnRefresh();
		break;
	case icmdVaCmd_RefResultsToggleHighlight:
		mResultsWnd->OnToggleHighlight();
		break;
	case icmdVaCmd_RefResultsCopy:
		mResultsWnd->OnCopy();
		break;
	case icmdVaCmd_RefResultsCopyAll:
		mResultsWnd->OnCopyAll();
		break;
	case icmdVaCmd_RefResultsCut:
		mResultsWnd->OnCut();
		break;
	case icmdVaCmd_RefResultsContextMenu:
		mResultsWnd->OnContextMenu(NULL, CPoint(0, 0));
		break;
	case icmdVaCmd_RefResultsSort:
	case icmdVaCmd_RefResultsExpand:
	case icmdVaCmd_RefResultsCollapse:
	case icmdVaCmd_RefResultsToggleTooltips:
		return E_UNEXPECTED;
	default:
		return OLECMDERR_E_NOTSUPPORTED;
	}

	return S_OK;
}

bool FindReferencesResultsFrame::IsWindowFocused() const
{
	if (m_hWnd && ::IsWindow(m_hWnd) && IsWindowVisible() && mResultsWnd)
	{
		CWnd* foc = GetFocus();
		if (foc)
		{
			if (foc == this || foc == mResultsWnd || foc == &mResultsWnd->m_treeSubClass ||
			    foc == &mResultsWnd->m_tree || foc == mResultsWnd->mEdit)
			{
				return true;
			}
		}
	}
	return false;
}

void FindReferencesResultsFrame::SetRefFlags(int flags)
{
	_ASSERTE(mResultsWnd);
	mResultsWnd->mRefs->flags = flags;
	if (flags & FREF_Flg_InFileOnly)
	{
		if (flags & FREF_Flg_CorrespondingFile)
			mResultsWnd->mRefs->SetScopeOfSearch(FindReferences::searchFilePair);
		else
			mResultsWnd->mRefs->SetScopeOfSearch(FindReferences::searchFile);
	}
}

WTString FindReferencesResultsFrame::GetRefSym() const
{
	return mResultsWnd->mRefs->GetFindSym();
}

void FindReferencesResultsFrame::FindSymbol(const WTString& sym, int imgIdx)
{
	_ASSERTE(mResultsWnd);
	mResultsWnd->FindCurrentSymbol(sym, imgIdx);
}

bool FindReferencesResultsFrame::RefsResultIsCloned() const
{
	_ASSERTE(mResultsWnd);
	return mResultsWnd->mHasClonedResults;
}

const FindReferences& FindReferencesResultsFrame::GetReferences() const
{
	_ASSERTE(mResultsWnd);
	return *(mResultsWnd->mRefs);
}

FindReferencesThread* FindReferencesResultsFrame::GetReferencesThread() const
{
	_ASSERTE(mResultsWnd);
	return mResultsWnd->mFindRefsThread;
}

void FindReferencesResultsFrame::DocumentModified(const WCHAR* filenameIn, int startLineNo, int startLineIdx,
                                                  int oldEndLineNo, int oldEndLineIdx, int newEndLineNo,
                                                  int newEndLineIdx, int editNo)
{
	if (mResultsWnd)
		mResultsWnd->mRefs->DocumentModified(filenameIn, startLineNo, startLineIdx, oldEndLineNo, oldEndLineIdx,
		                                     newEndLineNo, newEndLineIdx, editNo);
}

void FindReferencesResultsFrame::DocumentSaved(const WCHAR* filename)
{
	if (mResultsWnd)
		mResultsWnd->mRefs->DocumentSaved(filename);
}

void FindReferencesResultsFrame::DocumentClosed(const WCHAR* filename)
{
	if (mResultsWnd)
		mResultsWnd->mRefs->DocumentClosed(filename);
}

bool FindReferencesResultsFrame::IsThreadRunning() const
{
	if (mResultsWnd)
	{
		if (GetReferencesThread())
			return GetReferencesThread()->IsRunning();
	}

	return false;
}

void FindReferencesResultsFrame::ThemeUpdated()
{
	if (mResultsWnd)
		mResultsWnd->PrepareImages();
	RedrawWindow(NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

bool FindReferencesResultsFrame::Cancel()
{
	if (GetReferencesThread())
	{
		if (GetReferencesThread()->Cancel())
		{
			return false;
		}
	}

	return true;
}

bool FindReferencesResultsFrame::WaitForThread(DWORD waitLen)
{
	if (GetReferencesThread())
	{
		if (WAIT_TIMEOUT == GetReferencesThread()->Wait(waitLen))
			return false;
	}

	return true;
}

// PrimaryResultsFrame
// ----------------------------------------------------------------------------
//
PrimaryResultsFrame::PrimaryResultsFrame(int flags, int typeImageIdx, LPCSTR symScope) : FindReferencesResultsFrame()
{
	mResultsWnd = new FindUsageDlg;
	SetRefFlags(flags);

#ifdef RAD_STUDIO
	HWND hResultsPane = RS_ShowWindow(L"VA Find References");
#else
	HWND hResultsPane = gShellSvc->GetFindReferencesWindow();
#endif

	if (!hResultsPane || !SubclassWindow(hResultsPane) || !m_hWnd)
	{
		vLog("PrimaryResultsFrame::Ctor no hwnd");
		return;
	}

	if (!CommonCtor(hResultsPane))
		return;

	if (flags != 0)
	{
		WTString sScope;
		if (symScope)
			sScope = symScope;
		else if (g_currentEdCnt)
		{
			// symScope should only be NULL for FindErrors
			// (or in VC6 when opening the results wnd but flags will be 0 in that case)
			_ASSERTE(flags & FREF_Flg_FindErrors);
			sScope = g_currentEdCnt->GetSymScope();
		}
		FindSymbol(sScope, typeImageIdx);
	}
}

PrimaryResultsFrame::PrimaryResultsFrame(const FindReferences& refs)
{
	mResultsWnd = new FindUsageDlg(refs, false);
	SetRefFlags(refs.flags);
	HWND hResultsPane = gShellSvc->GetFindReferencesWindow();
	if (!hResultsPane || !SubclassWindow(hResultsPane) || !m_hWnd)
	{
		vLog("PrimaryResultsFrame::Ctor no hwnd");
		return;
	}

	if (!CommonCtor(hResultsPane))
		return;
}

PrimaryResultsFrame::~PrimaryResultsFrame()
{
	if (m_hWnd)
		UnsubclassWindow();
}

bool PrimaryResultsFrame::IsOkToClone() const
{
	if (mResultsWnd && GetFoundUsageCount() && !RefsResultIsCloned() &&
	    (!GetReferencesThread() || GetReferencesThread()->IsFinished()))
		return true;
	return false;
}

LPCTSTR
PrimaryResultsFrame::GetCaption()
{
	mCaption = "VA Find References Results";
	_ASSERTE(!mResultsWnd || (mResultsWnd && !RefsResultIsCloned()));
	return mCaption.c_str();
}

bool PrimaryResultsFrame::IsPrimaryResultAndIsVisible() const
{
	_ASSERTE(!RefsResultIsCloned());
	return m_hWnd && mResultsWnd && ::IsWindow(m_hWnd) && mResultsWnd->m_hWnd && mResultsWnd->IsWindowVisible();
}

// PrimaryResultsFrameVs
// ----------------------------------------------------------------------------
//

PrimaryResultsFrameVs::~PrimaryResultsFrameVs()
{
	// restore IDE refs window in case auto-hide is enabled.
	// We rely on SetFindReferencesWindow to give us the pane wnd that the
	// IDE has created for us to host our wnd.   When auto-hide is not
	// enabled, we get a notification before each search.  When auto-hide
	// is enabled, we don't get that notification for the primary wnd.
	// So we restore it here when our primary wnd is deleted to make way
	// for a new one (since a subsequent clone results command will have
	// overwritten the value needed for the next new search).
	if (m_hWnd && ::IsWindow(m_hWnd) && gShellSvc)
		gShellSvc->SetFindReferencesWindow(m_hWnd);
}

// PrimaryResultsFrameVc6
// ----------------------------------------------------------------------------
//

// leave search results intact, attach to new parent window
void PrimaryResultsFrameVc6::Reparent()
{
	_ASSERTE(!RefsResultIsCloned());
	if (IsWindow(mResultsWnd->m_hWnd))
		mResultsWnd->Hide(true);

	const HWND hOutput = gShellSvc->GetFindReferencesWindow();
	const bool doSubclassCycle = ::IsVc6MdiWnd(hOutput) || FromHandlePermanent(hOutput);
	if (m_hWnd && !doSubclassCycle)
		UnsubclassWindow();
	if (!hOutput)
		return;
	if (!m_hWnd)
	{
		if (!SubclassWindow(hOutput))
			return;
	}
	_ASSERTE(::IsWindow(m_hWnd));

	mResultsWnd->SetParent(this);
}

void PrimaryResultsFrameVc6::FocusFoundUsage()
{
	_ASSERTE(gShellAttr->RequiresFindResultsHack());
	Reparent();
	OnSize();
	mResultsWnd->Show(SW_SHOW);
	FindReferencesResultsFrame::FocusFoundUsage();
}

bool PrimaryResultsFrameVc6::HidePrimaryResults()
{
	// this only applies to the primary vc6 result window
	mResultsWnd->OnCancel();
	return true;
}

LRESULT
PrimaryResultsFrameVc6::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_DESTROY)
	{
		// in vc6, save primary results window for possible later restoration
		if (mResultsWnd && IsWindow(mResultsWnd->m_hWnd))
			mResultsWnd->Hide(true);
	}

	static const unsigned int WM_VA_CLOSE_FINDREF = ::RegisterWindowMessage("WM_VA_CLOSE_FINDREF");
	if (message == WM_VA_CLOSE_FINDREF)
	{
		if (mResultsWnd->m_hWnd && mResultsWnd->IsWindow())
		{
			mResultsWnd->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_CLOSE, BN_CLICKED));
		}
		return 0;
	}

	LRESULT r = PrimaryResultsFrame::WindowProc(message, wParam, lParam);
	if (WM_WINDOWPOSCHANGED == message && mResultsWnd && mResultsWnd->GetSafeHwnd())
	{
		if (IsPrimaryResultAndIsVisible() &&
		    !mResultsWnd->mInteractivelyHidden) // window is hidden while moved, so can't call IsWindowVisible
		{
			FocusFoundUsage();
		}
	}

	return r;
}

static bool IsVc6MdiWnd(HWND wnd)
{
	if (!wnd)
		return false;

	HWND lastWnd = wnd;
	for (;;)
	{
		HWND parent = GetParent(lastWnd);
		if (!parent || parent == lastWnd)
			return false;
		lastWnd = parent;
		WTString parentCls(GetWindowClassString(parent));
		if (parentCls == "MDIClient")
			return true;
	}
}

// SecondaryResultsFrame
// ----------------------------------------------------------------------------
//
LPCTSTR
SecondaryResultsFrame::GetCaption()
{
	if (mResultsWnd)
	{
		_ASSERTE(RefsResultIsCloned());
		mCaption.WTFormat("VA Find: %s", GetRefSym().c_str());
	}
	else
		mCaption = "VA Find References Results";
	return mCaption.c_str();
}

// SecondaryResultsFrameVs
// ----------------------------------------------------------------------------
//
SecondaryResultsFrameVs::SecondaryResultsFrameVs(const FindReferencesResultsFrame* refsToCopy) : SecondaryResultsFrame()
{
	if (!refsToCopy || !refsToCopy->mResultsWnd)
		return;

	mResultsWnd = new FindUsageDlg(refsToCopy->GetReferences(), true, refsToCopy->mResultsWnd->GetFilter());
	HWND hResultsPane = gShellSvc->GetFindReferencesWindow();
	if (!hResultsPane || !SubclassWindow(hResultsPane) || !m_hWnd)
		return;

	CommonCtor(hResultsPane);
}

SecondaryResultsFrameVs::~SecondaryResultsFrameVs()
{
	if (m_hWnd)
		UnsubclassWindow();
}

// SecondaryResultsFrameVc6
// ----------------------------------------------------------------------------
//
SecondaryResultsFrameVc6::SecondaryResultsFrameVc6(CWnd* vaFrameParent, const FindReferencesResultsFrame* refsToCopy)
    : SecondaryResultsFrame()
{
	if (!refsToCopy || !refsToCopy->mResultsWnd)
		return;

	mResultsWnd = new FindUsageDlg(refsToCopy->GetReferences(), true, refsToCopy->mResultsWnd->GetFilter());

	CRect rc;
	vaFrameParent->GetClientRect(rc);
	Create(AfxRegisterWndClass(0), "", WS_VISIBLE | WS_CHILD, rc, vaFrameParent, 1);
	if (!m_hWnd)
		return;

	CommonCtor(m_hWnd);
	GetCaption();
	vaFrameParent->SetWindowText(mCaption.c_str());
}
