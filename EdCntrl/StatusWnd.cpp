// StatusWnd.cpp : implementation file
//

#include "stdafxEd.h"
#include "Log.h"
#include "WTString.h"
#include "StatusWnd.h"
#include "Project.h"
#if _MSC_VER <= 1200
#include <../src/afximpl.h>
#else
#include <../atlmfc/src/mfc/afximpl.h>
#endif
#include "settings.h"
#include "VaService.h"
#include "DevShellService.h"
#include "DevShellAttributes.h"
#include "CodeGraph.h"
#include "SubClassWnd.h"

#ifdef RAD_STUDIO
#include "TraceWindowFrame.h"
#include "CppBuilder.h"
#include "SubClassWnd.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define IDT_SETTEXT 10
#define CMD_SetAnimation 350
#define CMD_ClearAnimation 351

// current statusbar text - can be modified from multiple threads.
// use a single shared buffer so that mem allocation/deallocation does
// not take place and no lock is required.
// It is always NULL terminated (extra byte at end that is never modified
// after init).
char gStatusTextBuf[StatusWnd::TxtBufLen + 1];

class StatusClass : public CWnd
{
  protected:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_SETTEXT || message == SB_SETTEXT)
		{
			if (lParam)
				::strncpy(gStatusTextBuf, (LPCSTR)lParam, StatusWnd::TxtBufLen - 1);
			else
				ZeroMemory(gStatusTextBuf, StatusWnd::TxtBufLen);
		}
		return CWnd::WindowProc(message, wParam, lParam);
	}

  public:
	StatusClass(HWND hstatus)
	{
		SubclassWindow(hstatus);
	}
};

static StatusClass* s_StatusClass;

void SetStatus(UINT nID, ...)
{
	CString frmt;
	WTString str;
	frmt.LoadString(nID);
	va_list args;
	va_start(args, nID);
	str.FormatV(frmt, args);
	va_end(args);
	SetStatus(str);
}
void SetStatusQueued(const char* format, ...)
{
	if (!g_statBar)
		return;

	auto str = std::make_unique<WTString>();
	va_list args;
	va_start(args, format);
	str->FormatV(format, args);
	va_end(args);

#ifndef VA_CPPUNIT
	extern std::atomic_uint64_t rfmt_status;
	++rfmt_status;
#endif
	RunFromMainThread2([str = std::move(str)] {
		if (g_statBar)
			g_statBar->SetStatusText(str->c_str());
	}, 1);
}

/////////////////////////////////////////////////////////////////////////////
// StatusWnd
#ifndef RAD_STUDIO
StatusWnd::StatusWnd() : mAnimType(IVaShellService::sa_none)
{
	ZeroMemory(mTxtBuf, TxtBufLen + 1);
	ZeroMemory(gStatusTextBuf, TxtBufLen + 1);
	m_StatBar = NULL;
	CRect rc(0, 0, 0, 0);
	VERIFY(Create(NULL, _T("VAStatMsgWnd"), 0, rc, GetDesktopWindow(), 100));
}

StatusWnd::~StatusWnd()
{
	CHandleMap* pMap = afxMapHWND();
	if (!pMap || !IsWindow(m_hWnd))
		m_hWnd = NULL;
	delete s_StatusClass;
	s_StatusClass = NULL;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(StatusWnd, CWnd)
//{{AFX_MSG_MAP(StatusWnd)
ON_WM_TIMER()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// StatusWnd message handlers
#if defined(VAX_CODEGRAPH)
static const CString kVaPrefix("Spaghetti: ");
#else
static const CString kVaPrefix("VA: ");
#endif


void StatusWnd::SetStatusText(LPCTSTR txt)
{
	CatLog("Parser.FileName", txt);

	KillTimer(IDT_SETTEXT);

	// save the text so that we don't block the calling thread
	if (txt && txt[0])
	{
		if (txt[0] == 'R' && _tcscmp(txt, "Ready") == 0)
		{
			::strcpy(mTxtBuf, "Ready");
		}
		else
		{
			static const int kVaPrefixLen = kVaPrefix.GetLength();
			::strcpy(mTxtBuf, kVaPrefix);
			::strncat(mTxtBuf, txt, size_t(StatusWnd::TxtBufLen - kVaPrefixLen - 1));
		}
	}
	else
		ZeroMemory(mTxtBuf, TxtBufLen);

	if (Psettings && Psettings->mEnableStatusBarMessages)
	{
		// use a timer in StatusWnd's thread to do the work of setting the text in the status bar.
		// using a timer means that some messages might not make it through to the status bar,
		// since there's only storage for one message at time - but every msg will get logged
		SetTimer(IDT_SETTEXT, 10, NULL);
	}
}

void StatusWnd::OnTimer(UINT_PTR nIDEvent)
{
	KillTimer(nIDEvent);
	if (Psettings && Psettings->mEnableStatusBarMessages && (gShellAttr && gShellAttr->IsDevenv10OrHigher()) ||
	    (m_StatBar && ::IsWindow(m_StatBar)))
	{
		try
		{
			if (g_CodeGraphWithOutVA && gDte && !m_StatBar && !gVaShellService && !mDteStatusBar)
				gDte->get_StatusBar(&mDteStatusBar);
			// Make local copy so that we have an immutable string to work with.
			// Don't use WTString because it will assert if mTxtBuf changes
			// after it has completed the copy.
			static char newTxt[TxtBufLen + 1];
			::strcpy(newTxt, mTxtBuf);
			// make sure we didn't grab a copy of m_txt while it was being modified
			// this check isn't really applicable now that there are pre-alloc'd buffers involved.
			if (newTxt[0] == kVaPrefix[0] || newTxt[0] == 'R')
			{
				CString currentTxt;

				if (gVaShellService)
					currentTxt = gVaShellService->GetStatusText();
				else if (s_StatusClass)
					currentTxt = gStatusTextBuf;
				else
				{
					DWORD_PTR res = 0;
					if (m_StatBar)
						::SendMessageTimeout(m_StatBar, SB_GETTEXT, 10, (LPARAM)currentTxt.GetBuffer(50), SMTO_NORMAL,
						                     1, &res);
					else if (g_CodeGraphWithOutVA && mDteStatusBar)
						mDteStatusBar->put_Text(CComBSTR(newTxt));
					currentTxt.ReleaseBuffer();
				}

				// allow overwrite only if text starts with kVaPrefix or is empty or is "Ready"
				if (!currentTxt.GetLength() || currentTxt == "Ready" || currentTxt == "Ausgabe" ||
				    currentTxt == "Bereit" || currentTxt == "-- NORMAL --" || // viemu case 58034
				    currentTxt == "-- INSERT --" ||                           // viemu case 58034
				    currentTxt.Find("IntelliSense") != -1 || // overwrite vs2005 "Updating intellisense..." messages
				    (gShellAttr->IsDevenv16OrHigher() && currentTxt[0] == 'R' &&
				     currentTxt.Find("Restore completed") ==
				         0) || // overwrite vs2019 "Restore completed" solution load messages
				    (gShellAttr->IsDevenv10OrHigher() &&
				     // [case: 110738]
				     ((currentTxt[0] == 'O' && 0 == currentTxt.Find("Optimizing")) ||
				      (currentTxt[0] == 'C' && 0 == currentTxt.Find("Checking")) ||
				      (currentTxt[0] == 'I' && 0 == currentTxt.Find("Initializing")) ||
				      (currentTxt[0] == 'P' && 0 == currentTxt.Find("Parsing")) ||
				      (currentTxt[0] == 'S' && 0 == currentTxt.Find("Scanning")))) ||
				    (currentTxt[0] == kVaPrefix[0] && 0 == currentTxt.Find(kVaPrefix)) // always overwrite va messages
				)
				{
					if (!::strcmp(newTxt, "Ready") && GlobalProject && GlobalProject->IsBusy())
						; // don't output Ready if project is still loading
					else if (gVaShellService)
						gVaShellService->SetStatusText(newTxt);
					else
					{
						if (m_StatBar)
						{
							::SendMessage(m_StatBar, WM_SETTEXT, 0, (LPARAM)(LPCTSTR)newTxt);
							::UpdateWindow(m_StatBar);
						}
						else if (mDteStatusBar)
							mDteStatusBar->put_Text(CComBSTR(newTxt));
					}
				}
				else
				{
#ifdef _DEBUGxxx
					CString msg;
					CString__FormatA(msg, "VaStatusSkip due to text: %s\r\n", currentTxt);
					::OutputDebugString(msg);
#endif
				}
			}
		}
		catch (...)
		{
			// catch in case reading m_txt while being modified in another thread
			VALOGEXCEPTION("SW:");
		}
	}
	else
	{
		m_StatBar = gShellSvc->LocateStatusBarWnd();
	}

	if (m_StatBar && !s_StatusClass && !gVaShellService)
		s_StatusClass = new StatusClass(m_StatBar);
}

LRESULT
StatusWnd::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (WM_COMMAND == message)
	{
		if (CMD_SetAnimation == wParam)
			return OnSetAnimation((IVaShellService::StatusAnimation)lParam);
		if (CMD_ClearAnimation == wParam)
			return OnClearAnimation();
	}
	return CWnd::WindowProc(message, wParam, lParam);
}

LRESULT
StatusWnd::OnSetAnimation(IVaShellService::StatusAnimation type)
{
	if (gVaShellService)
	{
		mAnimType = type;
		gVaShellService->SetStatusAnimation(TRUE, mAnimType);
	}
	return S_OK;
}

LRESULT
StatusWnd::OnClearAnimation()
{
	if (gVaShellService)
		gVaShellService->SetStatusAnimation(FALSE, mAnimType);
	mAnimType = IVaShellService::sa_none;
	return S_OK;
}

void StatusWnd::SetAnimation(IVaShellService::StatusAnimation type, bool asynchronous /*= true*/)
{
	if (asynchronous)
	{
		// post to main thread
		PostMessage(WM_COMMAND, CMD_SetAnimation, type);
	}
	else
		OnSetAnimation(type);
}

void StatusWnd::ClearAnimation(bool asynchronous /*= true*/)
{
	if (asynchronous)
	{
		// post to main thread
		PostMessage(WM_COMMAND, CMD_ClearAnimation);
	}
	else
		OnClearAnimation();
}
#endif

#ifdef RAD_STUDIO
void StatusWnd::SetStatusText(LPCTSTR txt)
{
	if (!gRadStudioHost || !txt || !*txt)
		return;

	CStringW wstr = ::MbcsToWide(txt, CString::StrTraits::SafeStringLen(txt));

	if (g_mainThread == ::GetCurrentThreadId())
	{
		gRadStudioHost->ShowStatusBegin(L"VA", L"");
		gRadStudioHost->ShowStatusReport(L"VA", wstr, -1);
		gRadStudioHost->ShowStatusEnd(L"VA", wstr);
	}
	else
	{
		RunFromMainThread([wstr]() {
			if (gRadStudioHost)
			{
				gRadStudioHost->ShowStatusBegin(L"VA", L"");
				gRadStudioHost->ShowStatusReport(L"VA", wstr, -1);
				gRadStudioHost->ShowStatusEnd(L"VA", wstr);
			}
		}, false);
	}

	auto tracer = gVaService->GetTraceFrame();
	if (tracer && tracer->IsTraceEnabled())
		tracer->Trace(txt);
}

StatusWnd::StatusWnd()
{
}

StatusWnd::~StatusWnd()
{
}


#endif