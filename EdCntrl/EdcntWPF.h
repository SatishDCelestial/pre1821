#pragma once

#include "WPF_ViewManager.h"
#include "VaService.h"
#include "StringUtils.h"
#include "focus.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#ifdef _WIN64
#include "..\VaManagedComLib\VaManagedComLib64_h.h"
#else
#include "..\VaManagedComLib\VaManagedComLib_h.h"
#endif

#include "mainThread.h"
#include "FontSettings.h"
#include "PROJECT.H"
#include "DevShellService.h"

extern CComQIPtr<IVaManagedComService> g_managedInterop;

class EdCntWPF : public EdCnt
{
  public:
	EdCntWPF() : m_rc(0, 0, 10, 10)
	{
	}

#pragma region IEdCnt
	virtual bool IsInitialized() const
	{
		_ASSERTE(!!m_WTManagedView);
		return !!m_WTManagedView;
	}

	virtual CPoint vGetCaretPos() const
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
		{
			// CaretScreenPos returns the wrong value after ctrl+up/dn
			//  			CPoint pt;
			//  			m_WTManagedView->CaretScreenPos(&pt.y, &pt.x);
			//  			WpfWnd()->ScreenToClient(&pt);
			//  			return pt;

			// Stripped logic from GetCharPos, to preserve "const"
			// Not very efficient, but it works.
			LONG l, c;
			PosToLC(GetBufConst(), (long)CurPos(), l, c, FALSE);
			CPoint pt;
			GetPointOfLineColumn(l - 1, c - 1, &pt);
			return pt + m_rc.TopLeft();
		}
		else
		{
#ifdef _DEBUG
			if (g_mainThread != GetCurrentThreadId())
				OutputDebugString("vGetCaretPos inaccurate - not on ui thread\n");
#endif // _DEBUG
		}

		if (m_WTManagedView)
		{
			vLog("WARN: EdWpf GCPos not UI thrd");
		}
		else
		{
			vLog("WARN: EdWpf CFP no WtMv - return bad pos");
		}

		_ASSERTE(!"Passed to CWnd::GetCaretPos in EdCntWPF!");

		return CWnd::GetCaretPos();
	}

	virtual void vClientToScreen(LPPOINT lpPoint) const
	{
		WPF_ParentWrapperPtr par = WpfWnd();
		CWnd* wnd = AltWnd(par.get());
		if (wnd)
			wnd->ClientToScreen(lpPoint);
	}

	virtual void vClientToScreen(LPRECT lpRect) const
	{
		WPF_ParentWrapperPtr par = WpfWnd();
		CWnd* wnd = AltWnd(par.get());
		if (wnd)
			wnd->ClientToScreen(lpRect);
	}

	virtual void vScreenToClient(LPPOINT lpPoint) const
	{
		WPF_ParentWrapperPtr par = WpfWnd();
		CWnd* wnd = AltWnd(par.get());
		if (wnd)
			wnd->ScreenToClient(lpPoint);
	}

	virtual void vScreenToClient(LPRECT lpRect) const
	{
		WPF_ParentWrapperPtr par = WpfWnd();
		CWnd* wnd = AltWnd(par.get());
		if (wnd)
			wnd->ScreenToClient(lpRect);
	}

	virtual CWnd* vGetFocus() const
	{
		return FromHandle(VAGetFocus());
	}

	virtual CWnd* vSetFocus()
	{
		CWnd* prevFocus = vGetFocus();
		OnSetFocus(prevFocus);
		if (GetIvsTextView())
			m_IVsTextView->SendExplicitFocus();
		vGetClientRect(m_rc);
		return prevFocus;
	}

	virtual CPoint GetCharPos(long lChar)
	{
#ifdef _DEBUG
		if (g_mainThread != GetCurrentThreadId())
			OutputDebugString("GetCharPos inaccurate - not on ui thread\n");
#endif // _DEBUG

		// Client pixel point from buffer index
		LONG l, c;
		PosToLC(GetBufConst(), lChar, l, c, FALSE);
		CPoint pt;
		UpdateLineHeight();
		GetPointOfLineColumn(l - 1, c - 1, &pt);
		vGetClientRect(m_rc);
		return pt + m_rc.TopLeft();
	}

	virtual int CharFromPos(POINT* pt, bool /*resolveVc6Tabs = false*/)
	{
		if (!GetIvsTextView())
		{
			vLog("WARN: EdWpf CFPos no ivstv");
			return 0;
		}

		// Buffer index from client pixel point
		CPoint cpt(*pt);
		cpt -= m_rc.TopLeft();
		long iMinUnit, iMaxUnit, iVisibleUnits, iFirstVisibleUnit;
		UpdateLineHeight();

		long firstVisLine, lastVisLine;
		GetVisibleLineRange(firstVisLine, lastVisLine);

		long line = firstVisLine - 1; // -1 for 0 offset.  Changed from GetScrollInfo(SB_VERT) for case=40784.
		m_IVsTextView->GetScrollInfo(SB_HORZ, &iMinUnit, &iMaxUnit, &iVisibleUnits, &iFirstVisibleUnit);
		long col = iFirstVisibleUnit;

		// in vs2010, iFirstVisibleUnit is in pixels (HORZ only).
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && m_WTManagedView && g_vaCharWidth[' '])
			col = iFirstVisibleUnit / (g_vaCharWidth[' '] * (long)m_WTManagedView->GetViewZoomFactor());

		//  Find line
		CPoint lpt(-1, -1);
		for (CPoint p(0, 0); GetPointOfLineColumn(line + 1, col, &p) == S_OK && p.y < cpt.y; line++)
		{
			if (lpt == p)
			{
				// this condition causes regression [case: 62685] since the pt for each
				// line in a set of folded lines is the same

				// only break if we've gone beyond last visible line
				if (line > lastVisLine)
				{
					// Prevent spin in debugger canvas [case=58124]
					break;
				}
			}
			lpt = p;
		}

		//  Find column
		lpt = CPoint(-1, -1);
		for (CPoint p(0, 0); GetPointOfLineColumn(line, col + 1, &p) == S_OK && p.x < cpt.x; col++)
		{
			if (lpt == p)
				break; // Prevent spin in debugger canvas,  [case=58124]
			lpt = p;
		}

		long offset, virtualSpaces;
		m_IVsTextView->GetNearestPosition(line, col, &offset, &virtualSpaces);
		return ::AdjustPosForMbChars(GetBuf(), offset);
	}

	virtual long GetFirstVisibleLine()
	{
		if (m_WTManagedView)
		{
			long first = 0;
			long last = 0;
			GetVisibleLineRange(first, last);
			return first;
		}
		else if (GetIvsTextView())
		{
			long iMinUnit, iMaxUnit, iVisibleUnits, iFirstVisibleUnit;
			m_IVsTextView->GetScrollInfo(SB_VERT, &iMinUnit, &iMaxUnit, &iVisibleUnits, &iFirstVisibleUnit);
			m_firstVisibleLine = iFirstVisibleUnit + 1;
			return m_firstVisibleLine;
		}
		else
		{
			return 1;
		}
	}

	virtual void ReserveSpace(int top, int left, int height, int width)
	{
		// reserves space via ISpaceReservationAgent
		if (m_WTManagedView)
			m_WTManagedView->ReserveSpace(top, left, height, width);
	}

	virtual bool IsQuickInfoActive()
	{
		_ASSERTE(g_mainThread == ::GetCurrentThreadId());
		// [case: 114018]
		// this is unreliable, only good enough for 15.6 because IQuickInfoSession
		// is so broken (IIntellisenseSession.Dismissed does not fire)
		_ASSERTE(gShellAttr->IsDevenv15u6OrHigher());
		if (m_WTManagedView)
			if (m_WTManagedView->IsQuickInfoActive())
				return true;
		return false;
	}

	virtual void GetVisibleLineRange(long& first, long& last)
	{
		if (m_WTManagedView)
		{
			long wpfFirst = 0;
			long wpfLast = 0;
			m_WTManagedView->GetVisibleLines(&wpfFirst, &wpfLast);
			first = wpfFirst + 1;
			last = wpfLast + 1;
			m_firstVisibleLine = first;
		}
		else
		{
			// same as EdCntCWnd
			first = GetFirstVisibleLine();
			CRect rc;
			vGetClientRect(&rc);
			last = first + rc.Height() / g_FontSettings->GetCharHeight();
		}
	}

	virtual bool TryToShow(long lStartPt, long lEndPt, HowToShow howToShow = HowToShow::ShowAsIs)
	{
		_ASSERTE(m_IVsTextView != nullptr);

		if (m_IVsTextView == nullptr)
			return false;

		TextSpan span;
		if (SUCCEEDED(m_IVsTextView->GetLineAndColumn(lStartPt, &span.iStartLine, &span.iStartIndex)) &&
		    SUCCEEDED(m_IVsTextView->GetLineAndColumn(lEndPt, &span.iEndLine, &span.iEndIndex)))
		{
			if (howToShow == HowToShow::ShowCentered)
			{
				return SUCCEEDED(m_IVsTextView->CenterLines(span.iStartLine, span.iEndLine));
			}
			else if (howToShow == HowToShow::ShowTop)
			{
				return SUCCEEDED(m_IVsTextView->SetTopLine(span.iStartLine));
			}
			else if (howToShow == HowToShow::ShowAsIs)
			{
				return SUCCEEDED(m_IVsTextView->EnsureSpanVisible(span));
			}

			long top_line = -1, bottom_line = -1;
			GetVisibleLineRange(top_line, bottom_line);

			if (top_line == -1 || bottom_line == -1)
				return SUCCEEDED(m_IVsTextView->EnsureSpanVisible(span));
			else
			{
				int lines_visible = bottom_line - top_line + 1;
				int lines_span = span.iEndLine - span.iStartLine + 1;

				if (lines_visible >= lines_span)
				{
					return SUCCEEDED(m_IVsTextView->EnsureSpanVisible(span));
				}
				else
				{
					return SUCCEEDED(m_IVsTextView->SetTopLine(span.iStartLine));
				}
			}
		}

		return false;
	}

	virtual void GetScrollInfo()
	{
		// don't call GetIvsTextView in here due to recursion
		if (!m_IVsTextView)
			return;

		if (gVaService)
		{
			// Update scrollbar info
			m_firstVisibleLine = GetFirstVisibleLine(); // m_IVsTextView check above needed to prevent recursion.

			long MinUnit, MaxUnit, VisibleUnits, FirstVisibleUnit;
			m_IVsTextView->GetScrollInfo(SB_HORZ, &MinUnit, &MaxUnit, &VisibleUnits, &FirstVisibleUnit);
			gVaService->UpdateActiveTextViewScrollInfo(m_IVsTextView, SB_HORZ, FirstVisibleUnit);
		}

		if (gVaInteropService)
			UpdateLineHeight();
	}

#pragma endregion IEdCnt

  private:
	IVsTextView* GetIvsTextView()
	{
		if (!m_IVsTextView)
		{
			if (WPF_ViewManager::Get())
			{
				SetIvsTextView(WPF_ViewManager::Get()->GetActiveView());
				GetScrollInfo();
			}

			if (!m_IVsTextView)
			{
				vLog("WARN: EdWpf GVsTv missing");
			}
		}
		return m_IVsTextView;
	}

// #VaManagedTextView
#pragma region IWTManagedView
	CComQIPtr<IVaManagedComTextView> m_WTManagedView;
	virtual void SetIvsTextView(IVsTextView* pView)
	{
		const bool getNewManagedView = pView != m_IVsTextView;
		__super::SetIvsTextView(pView);
		if (!pView)
			return;

		// [case: 75021] workaround for PeekDef window initially using wrong DTE doc ptr
		if (gShellAttr && gShellAttr->IsDevenv12OrHigher())
		{
			CComPtr<IVsTextLines> vsTextBuffer;
			pView->GetBuffer(&vsTextBuffer);
			if (vsTextBuffer)
			{
				CComQIPtr<IVsUserData> ud(vsTextBuffer);
				if (ud)
				{
					CComVariant bm;
					if (S_OK == ud->GetData(GUID_VsBufferMoniker, &bm))
					{
						CStringW file = bm.bstrVal;
						if (file != this->FileName())
						{
							// DTE Doc object is wrong, need to update it
							if (gDte)
							{
								CComPtr<EnvDTE::Documents> docs;
								gDte->get_Documents(&docs);
								if (docs)
								{
									CComVariant fileVar(file);
									CComPtr<EnvDTE::Document> doc;
									docs->Item(fileVar, &doc);
									if (doc)
									{
										auto docPtr = (EnvDTE::Document*)doc;
										SendVamMessage(VAM_UpdateDocDte, (WPARAM)m_VSNetDoc, (LPARAM)docPtr);
									}
								}
							}
						}
					}
				}
			}
		}

		// Get managed Com view
		if ((!m_WTManagedView || getNewManagedView) && g_managedInterop)
		{
#if _WIN64
			IUnknown* unk = (IUnknown*)(uintptr_t)g_managedInterop->GetVAManagedViewIUnkPtr((int64_t)(uintptr_t)pView);
#else
			IUnknown* unk = (IUnknown*)g_managedInterop->GetVAManagedViewIUnkPtr((int)pView);
#endif
			// gmit: I doubt this 0x80004002 works as a workaround
			if (unk &&
#if _WIN64
			    (DWORD)(uintptr_t)unk != 0x80004002
#else
			    (DWORD)unk != 0x80004002
#endif
			    ) // 0x80004002 == interface not supported
			{
				m_WTManagedView = unk;
				unk->Release();
			}
		}
	}

	virtual void vGetClientRect(LPRECT lpRect) const
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
		{
			auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

			m_WTManagedView->GetWindowRect(&lpRect->top, &lpRect->left, &lpRect->bottom, &lpRect->right);

			lpRect->bottom = lpRect->top + VsUI::DpiHelper::LogicalToDeviceUnitsY(lpRect->bottom - lpRect->top);
			lpRect->right = lpRect->left + VsUI::DpiHelper::LogicalToDeviceUnitsX(lpRect->right - lpRect->left);

			WPF_ParentWrapperPtr par = WpfWnd();
			CWnd* wnd = AltWnd(par.get());
			if (wnd)
				wnd->ScreenToClient(lpRect);
		}
		else
		{
#ifdef _DEBUG
			if (g_mainThread != GetCurrentThreadId())
				OutputDebugString("vGetClientRect inaccurate - not on ui thread\n");
#endif // _DEBUG
			WPF_ParentWrapperPtr par = WpfWnd();
			CWnd* wnd = AltWnd(par.get());
			if (wnd)
				wnd->GetClientRect(lpRect);
		}
	}

	void UpdateLineHeight()
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
		{
			auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

			int iLineHeight = m_WTManagedView->GetCaretTextLineHeight();
			if (iLineHeight && g_FontSettings)
			{
				iLineHeight = VsUI::DpiHelper::LogicalToDeviceUnitsY(iLineHeight);
				g_FontSettings->SetLineHeight(iLineHeight);
			}
		}
		else
		{
#ifdef _DEBUG
			if (g_mainThread != GetCurrentThreadId())
				OutputDebugString("UpdateLineHeight inaccurate - not on ui thread\n");
#endif // _DEBUG
		}
	}

	LRESULT GetPointOfLineColumn(long iLine, ViewCol iCol, POINT* ppt) const
	{
		if (!m_WTManagedView || g_mainThread != GetCurrentThreadId())
		{
#ifdef _DEBUG
			if (g_mainThread != GetCurrentThreadId())
				OutputDebugString("GetPointOfLineColumn inaccurate - not on ui thread\n");
#endif // _DEBUG
			return S_FALSE;
		}
		return m_WTManagedView->GetPointOfLineColumn(iLine, iCol, &ppt->y, &ppt->x);
	}

  public:
	long GetMarginWidth(const wchar_t* name)
	{
		if (name && *name && m_WTManagedView && g_mainThread == GetCurrentThreadId())
		{
			auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);
			return VsUI::DpiHelper::LogicalToDeviceUnitsX(m_WTManagedView->GetMarginWidth(name));
		}
		return 0;
	}

	double GetViewZoomFactor()
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return m_WTManagedView->GetViewZoomFactor();
		return 100;
	}

	void SetViewZoomFactor(double factor)
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			m_WTManagedView->SetViewZoomFactor(factor);
	}

	bool DisplayLineOnTop(int line, double vertical_distance)
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return 1 == m_WTManagedView->DisplayLineOnTop(line - 1, vertical_distance);
		return false;
	}

	bool DisplayLineOnBottom(int line, double vertical_distance)
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return 1 == m_WTManagedView->DisplayLineOnBottom(line - 1, vertical_distance);
		return false;
	}

	int LineNumberFromBufferPosition(int pos)
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return m_WTManagedView->LineNumberFromBufferPosition(pos) + 1;
		return -1;
	}

	bool ZoomToEnsureLinesVisible(int first, int last)
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return 1 == m_WTManagedView->ZoomToEnsureLinesVisible(first - 1, last - 1);
		return false;
	}

	bool ScrollToEnsureLinesVisible(int first, int last)
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return 1 == m_WTManagedView->ScrollToEnsureLinesVisible(first - 1, last - 1);
		return false;
	}

	bool MoveCaretToScreenPos(int x, int y)
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return 1 == m_WTManagedView->MoveCaretToScreenPos(y, x);
		return false;
	}

	bool IsMouseOver() // [case: 94287] determines if WPF EdCnt is under mouse cursor
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return 1 == m_WTManagedView->IsMouseOver();
		return false;
	}

	bool IsKeyboardFocusWithin() // [case: 165029] determines if WPF EdCnt contains keyboard focus
	{
		if (m_WTManagedView && g_mainThread == GetCurrentThreadId())
			return 1 == m_WTManagedView->IsKeyboardFocusWithin();
		return false;
	}

	// TODO integrate vCaretPos changes
	// 	CPoint vGetCaretPos()
	// 	{
	// 		if(m_WTManagedView)
	// 		{
	// 			CPoint pt;
	// 			m_WTManagedView->CaretScreenPos(&pt.y, &pt.x);
	// 			WpfWnd()->ScreenToClient(&pt);
	// 			return pt;
	// 		}
	// 		return CWnd::GetCaretPos();
	// 	}
	//

#pragma endregion IWTManagedView

  private:
	WPF_ParentWrapperPtr WpfWnd() const
	{
		// This is just the main window in VS10.
		if (WPF_ViewManager::Get())
			return WPF_ViewManager::Get()->GetWPF_ParentWindow(); // gMainWnd;
		return nullptr;
	}

	CWnd* AltWnd(WPF_ParentWrapper* parent) const
	{
		if (!parent || !::IsWindow(parent->GetSafeHwnd()))
		{
			vLog("WARN: EdWpf WpfW par(%p)", parent);
			CWnd* foc = CWnd::GetFocus();
			_ASSERTE(foc);
			return foc; // gMainWnd; // If last active parent was destroyed before this parent gets focus, just route to
			            // mainWnd.
		}

		return parent;
	}

	// Forward screen messages to WPF window.
	virtual void GetWindowRect(LPRECT lpRect) const
	{
		WPF_ParentWrapperPtr par = WpfWnd();
		CWnd* wnd = AltWnd(par.get());
		if (wnd)
			wnd->GetWindowRect(lpRect);
	}

	virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		// Pass Mouse and key presses to WPF window for them to process.
		if (message > WM_MOUSEFIRST && message <= WM_MOUSELAST)
			vCatLog("Editor.Events", "VaEventME Ed::Dwp hwnd=0x%p msg=0x%x wp=0x%zx lp=0x%zx", m_hWnd, message, (uintptr_t)wParam,
			     (uintptr_t)lParam);

		if (WPF_ViewManager::Get())
		{
			WPF_ParentWrapperPtr parent = WPF_ViewManager::Get()->GetWPF_ParentWindow();
			if (parent)
			{
				const EdCntPtr curEdCnt = g_currentEdCnt;
				const LRESULT r = parent->WPFWindowProc(message, wParam, lParam);
				if (r)
					return r;
				if (curEdCnt.get() == this && !g_currentEdCnt)
					return 1; // [case: 51306]
			}
		}

		// Let our wnd handle the rest.
		return __super::DefWindowProc(message, wParam, lParam);
	}

	virtual void ResetZoomFactor()
	{
		if (m_WTManagedView)
			m_WTManagedView->ResetZoomFactor();
	}

	virtual bool HasBlockOrMultiSelection() const
	{
#ifndef VA_CPPUNIT
		// [case: 117499]
		if (!gShellAttr)
			return false;

		if (gShellAttr->IsDevenv15u8OrHigher())
		{
			int val = m_WTManagedView && m_WTManagedView->HasBlockOrMultiSelection();
			if (-1 != val)
				return !!val;

			// back-end isn't hooked up for some reason
			// we can query for box selection via old API, but we won't
			// know about multi-selection
			return SendVamMessage(VAM_GETSELECTIONMODE, 0, 0) == 11;
		}
		else
			return gShellSvc && gShellSvc->HasBlockModeSelection(this);
#else
		return false;
#endif
	}

	CRect m_rc;
};
