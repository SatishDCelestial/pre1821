#pragma once

#if defined(VISUAL_ASSIST_X)
#include "DevShellAttributes.h"
#define CWNDSUBCLASS_DPI_AWARE
#endif

#ifdef CWNDSUBCLASS_DPI_AWARE
#include "CWndDpiAware.h"
#endif

#ifdef _DEBUG
#ifndef TRACE_
#define TRACE_(...)                                                                                                    \
	do                                                                                                                 \
	{                                                                                                                  \
	} while (false)
#endif
#endif

template <typename BASE>
class CWndSubclassW : public BASE
#ifdef CWNDSUBCLASS_DPI_AWARE
    ,
                      public CDpiAware<BASE>
#endif
{
#ifdef CWNDSUBCLASS_DPI_AWARE
  protected:
	HWND GetDpiHWND() override
	{
		return __super::GetSafeHwnd();
	}

	BOOL PreCreateWindow(CREATESTRUCT& cs) override
	{
		VsUI::CDpiAwareness::AssertUnexpectedDpiContext(__super::GetDpiContext());
		return __super::PreCreateWindow(cs);
	}

#endif
  public:
	BOOL SubclassWindowW(HWND hWnd)
	{
		if (!__super::Attach(hWnd))
			return FALSE;

		// allow any other subclassing to occur
		__super::PreSubclassWindow();

		// returns correct unicode text of control
		WNDPROC* lplpfn = __super::GetSuperWndProcAddr();
		WNDPROC oldWndProc;
		if (IsWindowUnicode(hWnd))
			oldWndProc = (WNDPROC)::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)::AfxGetAfxWndProc());
		else
			oldWndProc = (WNDPROC)::SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (INT_PTR)::AfxGetAfxWndProc());

		ASSERT(oldWndProc != ::AfxGetAfxWndProc());

		if (*lplpfn == NULL)
			*lplpfn = oldWndProc; // the first control of that type created
#ifdef _DEBUG
		else if (*lplpfn != oldWndProc)
		{
			TRACE_((int)traceAppMsg, 0, "Error: Trying to use SubclassWindow with incorrect CWnd\n");
			TRACE_((int)traceAppMsg, 0, "\tderived class.\n");
			// TRACE(traceAppMsg, 0, "\thWnd = $%08X (nIDC=$%08X) is not a %hs.\n", (UINT)(UINT_PTR)hWnd,
			// _AfxGetDlgCtrlID(hWnd), GetRuntimeClass()->m_lpszClassName);
			ASSERT(FALSE);
			// undo the subclassing if continuing after assert
			if (::IsWindowUnicode(hWnd))
				::SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
			else
				::SetWindowLongPtrA(hWnd, GWLP_WNDPROC, (INT_PTR)oldWndProc);
		}
#endif
		return TRUE;
	}

	HWND UnsubclassWindowW()
	{
		ASSERT(::IsWindow(__super::m_hWnd));

		// set WNDPROC back to original value
		WNDPROC* lplpfn = __super::GetSuperWndProcAddr();
		if (::IsWindowUnicode(__super::m_hWnd))
			SetWindowLongPtrW(__super::m_hWnd, GWLP_WNDPROC, (INT_PTR)*lplpfn);
		else
			SetWindowLongPtrA(__super::m_hWnd, GWLP_WNDPROC, (INT_PTR)*lplpfn);
		*lplpfn = NULL;

		// and Detach the HWND from the CWnd object
		return __super::Detach();
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_DESTROY)
			UnsubclassWindowW();

#ifdef CWNDSUBCLASS_DPI_AWARE
		LRESULT result = 0;
		if (__super::HandleWndMessage(message, wParam, lParam, result))
			return result;
#endif

		return __super::WindowProc(message, wParam, lParam);
	}
};

template <typename BASE> class CWndTextW : public CWndSubclassW<BASE>
{
  public:
	virtual bool GetText(CStringW& text) const
	{
#if defined(VISUAL_ASSIST_X)
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
		// [case: 134135] fix string for VS2003 and older
		if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
#endif
#endif
		{
			WNDPROC wndProc = __super::m_pfnSuper;
			if (wndProc == nullptr)
				wndProc = (WNDPROC)::GetWindowLongPtrW(__super::m_hWnd, GWLP_WNDPROC);

			// We must call original window's procedure to get correct text
			if (wndProc)
			{
				// If we are here, this window is subclassed
				int nLen = (int)::CallWindowProcW(wndProc, __super::m_hWnd, WM_GETTEXTLENGTH, 0, 0);
				::CallWindowProcW(wndProc, __super::m_hWnd, WM_GETTEXT, (WPARAM)(nLen + 1),
				                  (LPARAM)text.GetBufferSetLength(nLen));
				text.ReleaseBuffer();

				return true;
			}

			return false;
		}
#if defined(VISUAL_ASSIST_X)
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
		// [case: 134135] fix string for VS2003 and older
		int nLen = GetWindowTextLengthW(__super::m_hWnd);
		GetWindowTextW(__super::m_hWnd, text.GetBufferSetLength(nLen), nLen + 1);
		text.ReleaseBuffer();
		return true;
#endif
#endif
	}

	virtual bool SetText(const CStringW& text)
	{
#if defined(VISUAL_ASSIST_X)
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
		// [case: 134135] fix string for VS2003 and older
		if (gShellAttr && gShellAttr->IsDevenv8OrHigher())
#endif
#endif
		{
			WNDPROC wndProc = __super::m_pfnSuper;
			if (wndProc == nullptr)
				wndProc = (WNDPROC)::GetWindowLongPtrW(__super::m_hWnd, GWLP_WNDPROC);

			// We must call original window's procedure to get correct text
			if (wndProc)
			{
				// If we are here, this window is subclassed
				::CallWindowProcW(wndProc, __super::m_hWnd, WM_SETTEXT, 0, (LPARAM)(LPCWSTR)text);
				return true;
			}
			return false;
		}
#if defined(VISUAL_ASSIST_X)
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
		// [case: 134135] fix string for VS2003 and older
		return SetWindowTextW(__super::m_hWnd, (LPCWSTR)text) != FALSE;
#endif
#endif
	}

	virtual bool ReplaceText(const CStringW& text, BOOL allowUndo)
	{
		WNDPROC wndProc = __super::m_pfnSuper;
		if (wndProc == nullptr)
			wndProc = (WNDPROC)::GetWindowLongPtrW(__super::m_hWnd, GWLP_WNDPROC);

		// We must call original window's procedure to get correct text
		if (wndProc)
		{
			// If we are here, this window is subclassed
			::CallWindowProcW(wndProc, __super::m_hWnd, EM_REPLACESEL, (WPARAM)allowUndo, (LPARAM)(LPCWSTR)text);
			return true;
		}
		return false;
	}
};
