#include "StdAfxEd.h"
#include "TreeCtrlTT.h"
#include "FontSettings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IMPLEMENT_DYNAMIC(CTreeCtrlTT, CColorVS2010TreeCtrl)

const unsigned int WM_ITEM_JUST_INSERTED = ::RegisterWindowMessage("WM_ITEM_JUST_INSERTED");

CTreeCtrlTT::CTreeCtrlTT() : mLastTooltips(NULL)
{
	mFont.CreateStockObject(ANSI_VAR_FONT);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CTreeCtrlTT, CColorVS2010TreeCtrl)
ON_WM_MOUSEMOVE()
ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
ON_WM_CREATE()
ON_NOTIFY_REFLECT(NM_CLICK, &CTreeCtrlTT::OnNMClick)
ON_WM_KILLFOCUS()
ON_WM_KEYDOWN()
END_MESSAGE_MAP()
#pragma warning(pop)

void CTreeCtrlTT::PopTooltip()
{
	CToolTipCtrl* tips = GetToolTips();
	if (tips)
	{
		GetCursorPos(&mPoppedTooltipPos);
		if (::IsWindow(tips->m_hWnd))
			tips->Pop();
	}
	else
		mPoppedTooltipPos.SetPoint(0, 0);

	AFX_MODULE_THREAD_STATE* amts = AfxGetModuleThreadState();
	if (amts)
	{
		try
		{
			HWND tTip = amts->m_pToolTip ? amts->m_pToolTip->m_hWnd : NULL;
			if (tTip)
			{
				GetCursorPos(&mPoppedTooltipPos);
				CToolTipCtrl& ttip = *(CToolTipCtrl*)CToolTipCtrl::FromHandle(tTip);
				ttip.Pop();
			}
		}
		catch (...)
		{
		}
	}
}

bool CTreeCtrlTT::OkToShowTooltip() const
{
	CPoint newPt;
	GetCursorPos(&newPt);
	if (newPt != mPoppedTooltipPos)
		return true;
	return false;
}

void CTreeCtrlTT::OnMouseMove(UINT nFlags, CPoint point)
{
	// allow owner window to receive mouse events
	AFX_MODULE_STATE* ams = _AFX_CMDTARGET_GETSTATE();
	if (ams->m_pfnFilterToolTipMessage)
	{
		MSG msg = *(MSG*)CWnd::GetCurrentMessage();
		ams->m_pfnFilterToolTipMessage(&msg, this);
	}

	AFX_MODULE_THREAD_STATE* amts = AfxGetModuleThreadState();
	if (amts)
	{
		HWND new_tooltips = amts->m_pToolTip ? amts->m_pToolTip->m_hWnd : NULL;
		if (mLastTooltips != new_tooltips)
		{
			mLastTooltips = new_tooltips;
			if (new_tooltips)
			{
				// setup tooltip appearance when detected for the first time
				CToolTipCtrl& ttips = *(CToolTipCtrl*)CToolTipCtrl::FromHandle(new_tooltips);
				//				ttips.SetTipBkColor(RGB(255, 0, 0));
				//				ttips.SetFont(&mFont); // case:64523
				ttips.SetDelayTime(TTDT_INITIAL, 1000);
				ttips.SetDelayTime(TTDT_RESHOW, 100);
				ttips.SetDelayTime(TTDT_AUTOPOP, 32767);
				ttips.ModifyStyle(0, TTS_NOPREFIX);
			}
		}
	}

	CColorVS2010TreeCtrl::OnMouseMove(nFlags, point);
}

BOOL CTreeCtrlTT::OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* result)
{
	TOOLTIPTEXTW* tttw = (TOOLTIPTEXTW*)pNMHDR;

	AFX_MODULE_THREAD_STATE* amts = AfxGetModuleThreadState();
	if (!amts || (amts->m_pToolTip && amts->m_pToolTip->m_hWnd != pNMHDR->hwndFrom) || !OkToShowTooltip())
		return false;

	if (g_FontSettings)
	{
		((CToolTipCtrl*)CWnd::FromHandle(pNMHDR->hwndFrom))->SetMaxTipWidth(g_FontSettings->m_tooltipWidth - 100);
	}

	static wchar_t ttbuffer[4096];
	ttbuffer[0] = 0;
	tttw->lpszText = ttbuffer;

	CPoint mouse((LPARAM)::GetMessagePos());
	ScreenToClient(&mouse);

	NMTVGETINFOTIPW git;
	memset(&git, 0, sizeof(git));
	git.hdr.code = TVN_GETINFOTIPW;
	git.hdr.hwndFrom = *this;
	git.hdr.idFrom = (uint)GetDlgCtrlID();
	git.hItem = HitTest(mouse);
	git.cchTextMax = sizeof(ttbuffer) / sizeof(ttbuffer[0]) - 1;
	git.pszText = ttbuffer;
	GetParent()->SendMessage(WM_NOTIFY, (WPARAM)GetDlgCtrlID(), (LPARAM)&git);

	if (result)
		*result = NULL;
	return true;
}

INT_PTR
CTreeCtrlTT::OnToolHitTest(CPoint point, TOOLINFO* ti) const
{
	UINT flags = 0;
	HTREEITEM item = HitTest(point, &flags);
	if (flags & (TVHT_ONITEM | TVHT_ONITEMINDENT | TVHT_ONITEMRIGHT))
	{
		RECT rect;
		GetItemRect(item, &rect, true);
		ti->hwnd = m_hWnd;
		ti->uId = (UINT_PTR)item;
		ti->lpszText = LPSTR_TEXTCALLBACK;
		ti->rect = rect;
		ti->uFlags |= TTF_TRANSPARENT;
		ti->uFlags &= ~(TTF_CENTERTIP | TTF_TRACK | TTF_ABSOLUTE);
		return (INT_PTR)ti->uId;
	}

	return CColorVS2010TreeCtrl::OnToolHitTest(point, ti);
}

void CTreeCtrlTT::OnNMClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	UINT flags = 0;
	CPoint pt((LPARAM)::GetMessagePos());
	ScreenToClient(&pt);
	HTREEITEM item = HitTest(pt, &flags);
	if (item && (flags & TVHT_ONITEMRIGHT))
	{
		// since the double-click will be applied on the currently
		// selected item, no further double-click handling is needed
		SelectItem(item);
	}

	*pResult = 0;
}

void CTreeCtrlTT::OnKillFocus(CWnd* pNewWnd)
{
	PopTooltip();
	CColorVS2010TreeCtrl::OnKillFocus(pNewWnd);
}

void CTreeCtrlTT::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	PopTooltip();
	CColorVS2010TreeCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

HTREEITEM CTreeCtrlTT::InsertItem(LPTVINSERTSTRUCT lpInsertStruct)
{
	HTREEITEM item = CColorVS2010TreeCtrl::InsertItem(lpInsertStruct);
	if (item)
		OnItemJustInserted(item);
	return item;
}
HTREEITEM CTreeCtrlTT::InsertItem(UINT nMask, LPCTSTR lpszItem, int nImage, int nSelectedImage, UINT nState,
                                  UINT nStateMask, LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	//	HTREEITEM item = CColorVS2010TreeCtrl::InsertItem(nMask, lpszItem, nImage, nSelectedImage, nState, nStateMask,
	// lParam, hParent, hInsertAfter); 	Insert as a wide str to fix displaying of MBCS in outline. case=4983
	const int kBufLen = 511;
	const std::unique_ptr<WCHAR[]> bufVec(new WCHAR[kBufLen + 1]);
	WCHAR* wstr = &bufVec[0];
	int inputLen = strlen_i(lpszItem);
	inputLen = std::min(inputLen, kBufLen);
	int w = MultiByteToWideChar(CP_UTF8, 0, lpszItem, inputLen, wstr,
	                            kBufLen); // fails if input is too large for output buffer
	wstr[w] = L'\0';
	TVINSERTSTRUCTW tvis;
	tvis.hParent = hParent;
	tvis.hInsertAfter = hInsertAfter;
	tvis.item.mask = nMask;
	tvis.item.pszText = (LPWSTR)wstr;
	tvis.item.iImage = nImage;
	tvis.item.iSelectedImage = nSelectedImage;
	tvis.item.state = nState;
	tvis.item.stateMask = nStateMask;
	tvis.item.lParam = lParam;

	HTREEITEM item = (HTREEITEM)::SendMessageW(m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
	if (item)
		OnItemJustInserted(item);
	return item;
}
HTREEITEM CTreeCtrlTT::InsertItem(LPCTSTR lpszItem, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	HTREEITEM item = CColorVS2010TreeCtrl::InsertItem(lpszItem, hParent, hInsertAfter);
	if (item)
		OnItemJustInserted(item);
	return item;
}
HTREEITEM CTreeCtrlTT::InsertItem(LPCTSTR lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent,
                                  HTREEITEM hInsertAfter)
{
	HTREEITEM item = CColorVS2010TreeCtrl::InsertItem(lpszItem, nImage, nSelectedImage, hParent, hInsertAfter);
	if (item)
		OnItemJustInserted(item);
	return item;
}

void CTreeCtrlTT::OnItemJustInserted(HTREEITEM item)
{
	GetParent()->SendMessage(WM_ITEM_JUST_INSERTED, (WPARAM) static_cast<CTreeCtrl*>(this), (LPARAM)item);
}

HTREEITEM
CTreeCtrlTT::InsertItemW(UINT nMask, LPCWSTR lpszItem, int nImage, int nSelectedImage, UINT nState, UINT nStateMask,
                         LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	ASSERT(::IsWindow(m_hWnd));
	TVINSERTSTRUCTW tvis;
	tvis.hParent = hParent;
	tvis.hInsertAfter = hInsertAfter;
	tvis.item.mask = nMask;
	tvis.item.pszText = (LPWSTR)lpszItem;
	tvis.item.iImage = nImage;
	tvis.item.iSelectedImage = nSelectedImage;
	tvis.item.state = nState;
	tvis.item.stateMask = nStateMask;
	tvis.item.lParam = lParam;

	HTREEITEM item = (HTREEITEM)::SendMessageW(m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
	if (item)
		OnItemJustInserted(item);
	return item;
}

HTREEITEM
CTreeCtrlTT::InsertItemW(LPCWSTR lpszItem, HTREEITEM hParent /*= TVI_ROOT*/, HTREEITEM hInsertAfter /*= TVI_LAST*/)
{
	return InsertItemW(TVIF_TEXT, lpszItem, 0, 0, 0, 0, 0, hParent, hInsertAfter);
}

HTREEITEM
CTreeCtrlTT::InsertItemW(LPCWSTR lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent /*= TVI_ROOT*/,
                         HTREEITEM hInsertAfter /*= TVI_LAST*/)
{
	return InsertItemW(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE, lpszItem, nImage, nSelectedImage, 0, 0, 0, hParent,
	                   hInsertAfter);
}

CStringW CTreeCtrlTT::GetItemTextW(_In_ HTREEITEM hItem)
{
	ASSERT(::IsWindow(m_hWnd));
	TVITEMW item;
	item.hItem = hItem;
	item.mask = TVIF_TEXT;
	CStringW str;
	int nLen = 128;
	int nRes;
	do
	{
		nLen *= 2;
		item.pszText = str.GetBufferSetLength(nLen);
		item.cchTextMax = nLen;
		::SendMessageW(m_hWnd, TVM_GETITEMW, 0, (LPARAM)&item);
		nRes = lstrlenW(item.pszText);
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}

BOOL CTreeCtrlTT::SetItemTextW(_In_ HTREEITEM hItem, _In_z_ LPCWSTR lpszItem)
{
	ASSERT(::IsWindow(m_hWnd));
	TVITEMW item;
	item.hItem = hItem;
	item.mask = TVIF_TEXT;
	item.pszText = (LPWSTR)lpszItem;
	return (BOOL)::SendMessageW(m_hWnd, TVM_SETITEMW, 0, (LPARAM)&item);
}
