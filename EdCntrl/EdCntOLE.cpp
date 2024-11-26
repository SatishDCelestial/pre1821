#include "stdafxed.h"
#include "edcnt.h"
#include "../Addin/DSCmds.h"
#include "BorderCWnd.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "fdictionary.h"
#include "FontSettings.h"
#include "SyntaxColoring.h"
#include "ScreenAttributes.h"
#include "../common/TempAssign.h"

IMPLEMENT_DYNAMIC(EdCnt, CWnd)

void EdCnt::OnLButtonDown(UINT nFlags, CPoint point)
{
	DISABLECHECK();
	// for DragDrop
	// SetCursor(LoadCursor(0, IDC_IBEAM)); // show ibeam on frag-selection
	OldLButtonDown(nFlags, point);
}

void EdCnt::OnContextMenu(CWnd* pWnd, CPoint point)
{
	CPoint pt = vGetCaretPos();
	pt.y += g_FontSettings->GetCharHeight();

	// [case: 91514] preserve old behavior, so that old VA Context Menu
	// opens when user has enabled its opening on Shift+Right-Click
	const bool oldContextMenuOnShiftKey = Psettings->mMouseClickCmds.get((DWORD)VaMouseCmdBinding::ShiftRightClick) ==
	                                      (DWORD)VaMouseCmdAction::ContextMenuOld;

	if (gShellAttr->IsDevenv())
	{
		bool doSpell = false;
		WTString cwd;

		// (pt.x - 1) to mimic GetCaretPos call in Edcnt::OnPaint
		AttrClassPtr attr = g_ScreenAttrs.AttrFromPoint(mThis, pt.x - 1, pt.y - g_FontSettings->GetCharHeight(), NULL);
		g_StrFromCursorPosUnderlined = (attr && attr->mFlag == SA_UNDERLINE_SPELLING);
		if (g_StrFromCursorPosUnderlined)
		{
			cwd = attr->mSym;
			doSpell = true;
		}

		if (doSpell && cwd.GetLength())
		{
			const WTString scope = Scope();
			if (scope.c_str()[0] != DB_SEP_CHR)
			{
				DisplaySpellSuggestions(pt, cwd);
				return;
			}
		}

		TempTrue tt(m_contextMenuShowing);
		if (oldContextMenuOnShiftKey && (GetKeyState(VK_SHIFT) & 0x1000))
		{
			if (point.x == 0xffffffff) // via syskey
			{
				m_lborder->DisplayMenu(pt); // place under caret
			}
			else // via right click
			{
				vScreenToClient(&point); // place at mouse cord
				m_lborder->DisplayMenu(point);
			}
		}
		else
			CTer::OnContextMenu(pWnd, point);
		return;
	}

	pt.x += LMARGIN;
	OnRButtonDown(0, pt);

	// Shift+F10 (equivalent of context menu key) breaks this logic
	//  too bad - it'll just be a side-effect of having
	//  m_contextMenuOnShift enabled
	if (!(oldContextMenuOnShiftKey && GetKeyState(VK_SHIFT) & 0x1000))
		OnRButtonUp(0, pt); // don't do OnRButtonUp if we'll display our menu
}
