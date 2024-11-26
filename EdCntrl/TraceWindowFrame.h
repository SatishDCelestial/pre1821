#pragma once

#include "VADialog.h"
#include "VsToolWindowPane.h"
#include "WTString.h"
#include "Menuxp\MenuXP.h"
#include "VaService.h"
#include "LOG.H"

#include <stack>
#include <vector>
#include <functional>

class CTreeCtrlTT;

class TraceWindowFrame : public VADialog
{
	DECLARE_MENUXP()

  public:
	TraceWindowFrame(HWND hWndParent);
	~TraceWindowFrame();

	// tracing methods
	bool IsTraceEnabled() const
	{
		return mTraceEnabled;
	}
	void Trace(const WTString txt);
	void OpenParent(const WTString txt);
	void CloseParent();

	// called from package for IDE command integration
	DWORD QueryStatus(DWORD cmdId) const;
	HRESULT Exec(DWORD cmdId);

	void IdePaneActivated();
	bool IsWindowFocused() const;

  protected:
	afx_msg void OnCopy();
	afx_msg void OnCut();
	afx_msg void OnDelete();
	afx_msg void OnClear();
	afx_msg void OnToggleTrace();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnExecAsync(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

	virtual BOOL OnInitDialog();

  private:
	HTREEITEM GetItemFromPos(POINT pos) const;
	void Trace(const WTString& txt, bool createAsParent);
	CStringW CopyHierarchy(HTREEITEM item, CStringW prefix);
	void PushAsyncAction(std::function<void()> fnc);

  private:
	CTreeCtrlTT* mTree;
	VsToolWindowPane mParent;
	CCriticalSection mParentsCs;
	std::stack<HTREEITEM> mParentItems;
	bool mTraceEnabled;

	CCriticalSection mAsyncActionsCs;
	std::vector<std::function<void()>> mAsyncActions;
};

// TraceHelp
// ----------------------------------------------------------------------------
// Use to output a trace statement
//
class TraceHelp
{
  protected:
	TraceHelp()
	    : mTracer(gVaService ? gVaService->GetTraceFrame() : NULL), mTraceEnabled(mTracer && mTracer->IsTraceEnabled())
	{
	}

  public:
	TraceHelp(const WTString txt)
	    : mTracer(gVaService ? gVaService->GetTraceFrame() : NULL), mTraceEnabled(mTracer && mTracer->IsTraceEnabled())
	{
		if (mTraceEnabled)
			mTracer->Trace(txt);
		vCatLog("LogSystem.Trace", "Trace: %s\n", txt.c_str());
	}

	bool IsTraceEnabled() const
	{
		return mTraceEnabled;
	}

  protected:
	TraceWindowFrame* mTracer;
	bool mTraceEnabled;
};

// ScopedTrace
// ----------------------------------------------------------------------------
// Use to output a start and end trace statement (as sibling nodes)
//
class ScopedTrace : public TraceHelp
{
  public:
	ScopedTrace(const WTString txt) : TraceHelp(txt), mText(txt)
	{
	}

	~ScopedTrace()
	{
		if (mTraceEnabled)
			mTracer->Trace("End " + mText);
		vCatLog("LogSystem.Trace", "Trace: [End] %s\n", mText.c_str());
	}

  private:
	WTString mText;
};

// TraceScopeExit
// ----------------------------------------------------------------------------
// Use to output a trace statement when scope ends (like ScopedTrace without a start statement)
//
class TraceScopeExit : public TraceHelp
{
  public:
	TraceScopeExit() : TraceHelp()
	{
	}
	TraceScopeExit(const WTString txt) : TraceHelp(), mText(txt)
	{
	}

	~TraceScopeExit()
	{
		if (mTraceEnabled)
			mTracer->Trace(mText);
		vCatLog("LogSystem.Trace", "Trace: %s\n", mText.c_str());
	}

	void UpdateTraceText(const WTString txt)
	{
		mText = txt;
	}

  private:
	WTString mText;
};

// NestedTrace
// ----------------------------------------------------------------------------
// Use to create a new trace group
// Closes the group when the instance goes out of scope
// Optionally traces an end statement as a child of the parent
//
class NestedTrace : public TraceHelp
{
  public:
	NestedTrace(const WTString txt, bool traceCompletion = false) : TraceHelp(), mTraceCompletion(traceCompletion)
	{
		if (mTraceEnabled)
			mTracer->OpenParent(txt);
		vCatLog("LogSystem.Trace", "Trace: [Open] %s\n", txt.c_str());
	}

	~NestedTrace()
	{
		if (mTraceEnabled)
		{
			if (mTraceCompletion)
				mTracer->Trace("Complete");
			mTracer->CloseParent();
		}

		if (mTraceCompletion)
			vCatLog("LogSystem.Trace", "Trace: [Close]\n");
	}

  protected:
	bool mTraceCompletion;
};

// TimeTrace
// ----------------------------------------------------------------------------
// Use to create a new timed trace group
// Closes the group when the instance goes out of scope
// Traces an end statement as a child of the parent
//
class TimeTrace : public TraceHelp
{
  public:
	TimeTrace(const WTString txt) : TraceHelp(), mStartTime(::GetTickCount())
	{
		if (mTraceEnabled)
			mTracer->OpenParent(txt);

		if (g_loggingEnabled)
		{
			mTxt = txt;
			vCatLog("LogSystem.Trace", "Trace: [Start] %s\n", txt.c_str());
		}
	}

	~TimeTrace()
	{
		if (mTraceEnabled || g_loggingEnabled)
		{
			const DWORD endTime = ::GetTickCount();
			const DWORD duration = endTime - mStartTime;
			WTString trcMsg;
			if (duration > (1000 * 60 * 60 * 2)) // greater than 2 hours? display in hours
				trcMsg.WTFormat("- elapsed %.02f hr", (float)duration / 3600000.f);
			else if (duration > (1000 * 60 * 60)) // greater than an hour? display in minutes
				trcMsg.WTFormat("- elapsed %.02f min", (float)duration / 60000.f);
			else if (duration > (1000 * 60)) // greater than a minute? display in seconds
				trcMsg.WTFormat("- elapsed %.02f sec", (float)duration / 1000.f);
			else // display in ms
				trcMsg.WTFormat("- elapsed %lu ms", duration);

			if (mTraceEnabled)
			{
				mTracer->Trace(trcMsg);
				mTracer->CloseParent();
			}

			vCatLog("LogSystem.Trace", "Trace: [End] %s %s\n", mTxt.c_str(), trcMsg.c_str());
		}
	}

  private:
	const DWORD mStartTime;
	WTString mTxt;
};
