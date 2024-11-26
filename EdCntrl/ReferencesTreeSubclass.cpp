#include "StdAfxEd.h"
#include "ReferencesTreeSubclass.h"
#include "ReferencesWndBase.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(ReferencesTreeSubclass, CColumnTreeWnd)
ON_NOTIFY(NM_RETURN, TreeID, OnSelect)
ON_NOTIFY(NM_CLICK, TreeID, OnClickTree)
ON_NOTIFY(NM_DBLCLK, TreeID, OnDblClickTree)
END_MESSAGE_MAP()
#pragma warning(pop)

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

LRESULT
ReferencesTreeSubclass::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT r = __super::WindowProc(message, wParam, lParam);

	if (WM_KILLFOCUS == message || WM_KEYDOWN == message)
	{
		if ((message == WM_KILLFOCUS && (::GetKeyState(VK_ESCAPE) & 0x1000)) ||
		    (message == WM_KEYDOWN && wParam == VK_ESCAPE))
			m_parent.OnTreeEscape();

		if (WM_KILLFOCUS == message)
			GetTreeCtrl().PopTooltip();
	}

	return r;
}

void ReferencesTreeSubclass::OnDblClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 1;
	m_parent.OnDoubleClickTree();
}

void ReferencesTreeSubclass::OnClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;

	DWORD dwpos = ::GetMessagePos();
	TVHITTESTINFO ht = {{0}};
	ht.pt.x = GET_X_LPARAM(dwpos);
	ht.pt.y = GET_Y_LPARAM(dwpos);
	::MapWindowPoints(HWND_DESKTOP, pNMHDR->hwndFrom, &ht.pt, 1);

	TreeView_HitTest(pNMHDR->hwndFrom, &ht);

	if (TVHT_ONITEMSTATEICON & ht.flags)
	{
		// clicked to check/un-check
		BOOL checking = !m_Tree.GetCheck(ht.hItem);
		HTREEITEM refItems = m_Tree.GetChildItem(ht.hItem);
		// if file check/un-check children
		while (refItems)
		{
			m_Tree.SetCheck(refItems, checking);
			refItems = m_Tree.GetNextSiblingItem(refItems);
		}
	}
}

void ReferencesTreeSubclass::OnSelect(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 1;
	m_parent.GoToSelectedItem();
}
