#pragma once

#include "DevShellAttributes.h"

class VsToolWindowPane : public CWnd
{
  public:
	VsToolWindowPane(HWND hWnd)
	{
		if (hWnd)
			SubclassWindow(hWnd);
		else
		{
			_ASSERTE(gShellAttr->IsMsdevOrCppBuilder());
		}
	}

	~VsToolWindowPane()
	{
		if (m_hWnd && IsWindow(m_hWnd))
			UnsubclassWindow();
	}

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_SIZE:
		case WM_PAINT: {
			CWnd::WindowProc(message, wParam, lParam);
			CWnd* nxt = GetWindow(GW_CHILD);
			if (nxt)
			{
				CRect r;
				GetClientRect(&r);
				nxt->MoveWindow(&r);
				nxt = nxt->GetWindow(GW_HWNDNEXT);
				if (nxt)
					nxt->MoveWindow(&r);
			}
			return TRUE;
		}
		case WM_SETFOCUS:
			if (!gShellAttr->IsDevenv())
			{
				HWND ch = ::GetWindow(m_hWnd, GW_CHILD);
				ch = ::GetWindow(ch, GW_HWNDNEXT);
				while (::GetWindow(ch, GW_CHILD))
					ch = ::GetWindow(ch, GW_CHILD);
				if (ch)
				{
					::SetFocus(ch);
					return TRUE;
				}
			}
			break;
		case WM_CHAR:
			return TRUE;
		}

		return CWnd::WindowProc(message, wParam, lParam);
	}
};
