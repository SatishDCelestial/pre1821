// FastComboBox.cpp : implementation file
//

#include "stdafxed.h"
#include "edcnt.h"
#include "FastComboBox.h"
#include "resource.h"
#include "WorkSpaceTab.h"
#include "DevShellAttributes.h"
#include "WindowUtils.h"
#include "ColorListControls.h"
#include "vsshell100.h"
#include "ImageListManager.h"
#include "SyntaxColoring.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define TT_TEXT "Select an entry to Goto. Type a few characters to filter list."
ComboSubClass3::ComboSubClass3() : ToolTipWrapper<CWndTextW<ComboPaintSubClass>>(NULL, TT_TEXT, 20, FALSE)
{
}

LRESULT ComboSubClass3::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK)
	{
		m_combo->PostMessage(message, wParam, lParam);
		return TRUE;
	}
	if (message == WM_KEYDOWN)
	{
		if (wParam != VK_DOWN && wParam != VK_UP && wParam != VK_NEXT && wParam != VK_PRIOR)
			m_combo->OnDropdown();
		m_combo->m_lstCombo.SendMessage(message, wParam, lParam);
		return TRUE;
	}
	return __super::WindowProc(message, wParam, lParam);
}

void ComboSubClass3::Init(CFastComboBox* combo)
{
	m_combo = combo;
	SubclassWindowW(combo->m_hCombo);
}

BOOL ComboSubClass3::OkToDisplayTooltip()
{
	if (m_combo && m_combo->IsListDropped())
		return FALSE;
	return TRUE;
}

IMPLEMENT_DYNAMIC(ComboEditSubClass3, CWnd)
ComboEditSubClass3::ComboEditSubClass3() : ToolTipWrapper<CWndTextW<CEdit>>(NULL, TT_TEXT, 10, FALSE)
{
}

LRESULT ComboEditSubClass3::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	// case: 147843 let CTRL+BACKSPACE delete the last word
	if (!m_auto_complete)
	{
		m_auto_complete = true;
		::SHAutoComplete(m_hWnd, SHACF_AUTOAPPEND_FORCE_OFF | SHACF_AUTOSUGGEST_FORCE_OFF);
	}

	if (message == WM_SETFOCUS)
	{
		CPoint pt;
		GetCursorPos(&pt);
		CRect rc;
		GetWindowRect(&rc);
		if (rc.PtInRect(pt) && (GetKeyState(VK_LBUTTON) & 0x1000))
		{
			LRESULT res = CWnd::WindowProc(message, wParam, lParam);
			if (!m_combo->IsListDropped())
				m_combo->OnDropdown();
			return res;
		}
	}
	if (message == WM_LBUTTONDOWN)
	{
		if (!m_combo->IsListDropped())
		{
			LRESULT res = CWnd::WindowProc(message, wParam, lParam);
			m_combo->OnDropdown();
			return res;
		}
	}
	if (message == WM_KILLFOCUS)
	{
		if ((HWND)wParam != m_combo->m_lstCombo.m_hWnd && (HWND)wParam != m_combo->m_hWnd)
			m_combo->DisplayList(FALSE);
	}
	if (message == WM_CHAR || message == WM_KEYDOWN || message == WM_COMMAND)
	{
		if (gShellAttr->IsMsdev() && message == WM_KEYDOWN && (wParam == VK_NEXT || wParam == VK_PRIOR) &&
		    (GetKeyState(VK_CONTROL) & 0x1000))
		{
			_ASSERTE(g_WorkSpaceTab);
			g_WorkSpaceTab->SwitchTab(wParam == VK_NEXT);
			return TRUE;
		}

		if (wParam == VK_ESCAPE)
		{
			if (g_currentEdCnt)
			{
				bool didSetFocus(false);
				const CWnd* par = g_currentEdCnt->GetParent();
				if (par)
				{
					CWnd* par2 = par->GetParent();
					if (par2)
					{
						par2->SetFocus();
						didSetFocus = true;
					}
				}

				if (!didSetFocus)
					g_currentEdCnt->vSetFocus();
			}
			else if (gShellAttr->IsDevenv())
			{
				HWND foc = m_hWnd;
				for (int idx = 0; idx < 5; ++idx)
				{
					HWND tmp = ::GetParent(foc);
					if (tmp)
						foc = tmp;
				}
				::SetFocus(foc);
			}
			else
				AfxGetMainWnd()->SetFocus();
		}
		else if (message == WM_CHAR && wParam == VK_RETURN)
		{
			m_combo->OnSelect();
			m_combo->DisplayList(FALSE);
			return TRUE;
		}
	}
	else if (message == WM_SYSKEYDOWN && (gShellAttr->IsMsdev() || VK_DOWN == wParam || VK_UP == wParam))
	{
		// [case: 58357] support for alt+up/down opening FIS / SIS lists -
		//		but not alt by itself.  In vs, lone alt takes focus to menubar.
		// [case: 841] vc6
		if (!m_combo->GetDroppedState())
			m_combo->OnDropdown();
		return FALSE;
	}
	return CWnd::WindowProc(message, wParam, lParam);
}

void ComboEditSubClass3::Init(CFastComboBox* combo)
{
	m_combo = combo;
	SubclassWindowW(combo->GetEditCtrl()->m_hWnd);
}

BOOL ComboEditSubClass3::OkToDisplayTooltip()
{
	if (m_combo && m_combo->IsListDropped())
		return FALSE;
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CFastComboBox

CFastComboBox::CFastComboBox(bool hasColorableContent)
    : CWndDpiAware<CComboBoxEx>(), CVAMeasureItem(this), mHasColorableContent(hasColorableContent)
{
	m_nDroppedWidth = 0;
	m_nDroppedCount = 0;
	popup_background_gradient_cache.reset(new CGradientCache);
}

CFastComboBox::~CFastComboBox()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CFastComboBox, CComboBox)
//{{AFX_MSG_MAP(CFastComboBox)
ON_WM_LBUTTONDOWN()
ON_WM_CREATE()
ON_WM_LBUTTONDBLCLK()
ON_WM_MEASUREITEM()
ON_CONTROL_REFLECT(CBN_DROPDOWN, OnDropdown)
ON_CONTROL_REFLECT(CBN_EDITCHANGE, OnEditChange)
//}}AFX_MSG_MAP
ON_WM_DRAWITEM()
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CFastComboBox message handlers

void CFastComboBox::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (!IsWindow())
		Init();

	if (m_lstCombo.IsWindowVisible())
		DisplayList(FALSE);
	else
		OnDropdown();
}

void CFastComboBox::SetItem(LPCSTR txt, int image)
{
	auto wt = WTString(txt);
	auto wide = wt.Wide();
	SetItemW(wide, image);
}

void CFastComboBox::SetItemW(LPCWSTR txt, int image)
{
	COMBOBOXEXITEMW cbi;
	ZeroMemory(&cbi, sizeof(COMBOBOXEXITEMW));
	cbi.iItem = 0;
	cbi.iSelectedImage = cbi.iImage = image;
	cbi.cchTextMax = wcslen_i(txt);
	cbi.pszText = (LPWSTR)txt;
	cbi.mask = CBEIF_IMAGE | CBEIF_INDENT | CBEIF_OVERLAY | CBEIF_SELECTEDIMAGE | CBEIF_TEXT | CBEIF_LPARAM;

	if (-1 != SendMessage(CBEM_INSERTITEMW, 0, (LPARAM)&cbi))
	{
		SetCurSel(0);
		if (GetEditCtrl())
			GetEditCtrl()->SetSel(0, 0);
	}
}

void CFastComboBox::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	OnLButtonDown(nFlags, point);
}

void CFastComboBox::DisplayList(BOOL bDisplay /* = TRUE*/)
{
	auto dpiScope = SetDefaultDpiHelper();

	WTString defaultTitle;
	int titleIconIdx;
	GetDefaultTitleAndIconIdx(defaultTitle, titleIconIdx);

	if (!AllowPopup())
		bDisplay = false;

	if (bDisplay)
	{
		CStringW txt;
		::GetWindowTextW(GetEditCtrl()->GetSafeHwnd(), txt);
		if (txt == defaultTitle.Wide())
			SetItem("", titleIconIdx);
	}
	else
		SetItemW(defaultTitle.Wide(), titleIconIdx);

	if (!bDisplay)
	{
		if (GetDroppedState())
			ReleaseCapture();
		if (IsWindow())
			m_lstCombo.ShowWindow(SW_HIDE);

		OnCloseup();
		return;
	}
	else if (!IsWindow())
		Init();

	if (bDisplay && GetDroppedState())
		return;

	CRect rc;
	GetWindowRect(rc);
	SetDroppedWidth((UINT)rc.Width());

	rc.top = rc.bottom;
	rc.right = rc.left + GetDroppedWidth();
	rc.bottom = rc.top + GetDroppedHeight();

	DoMultipleDPIsWorkaround(m_hCombo);

	m_lstCombo.Display(rc);
}
bool CFastComboBox::IsListDropped() const
{
	return m_lstCombo.m_hWnd && ::IsWindow(m_lstCombo.m_hWnd) && m_lstCombo.IsWindowVisible();
}

int CFastComboBox::GetDroppedHeight()
{
	// calculate dropped height from items count

	int itemHeight = MeasureItemHeight();

	if (itemHeight < 16)
		itemHeight = 16;

	return m_nDroppedCount * itemHeight;
}

int CFastComboBox::GetDroppedWidth()
{
	return m_nDroppedWidth;
}

int CFastComboBox::SetDroppedHeight(UINT nHeight)
{
	// convert height to visible count, so it is DPI independent value

	int itemHeight = MeasureItemHeight();

	if (itemHeight < 16)
		itemHeight = 16;

	m_nDroppedCount = int(nHeight / itemHeight);

	if (m_nDroppedCount < 1)
		m_nDroppedCount = 1;

	return m_nDroppedCount * itemHeight;
}

int CFastComboBox::SetDroppedWidth(UINT nWidth)
{
	m_nDroppedWidth = (int)nWidth;
	return (int)nWidth;
}

BOOL CFastComboBox::GetDroppedState() const
{
	return m_lstCombo.IsWindowVisible();
}

void CFastComboBox::PreSubclassWindow()
{
	CComboBoxEx::PreSubclassWindow();
}

void CFastComboBox::Init()
{
	auto dpiScope = SetDefaultDpiHelper();

	if (!mHasColorableContent)
		mySetProp(m_hWnd, "__VA_do_not_colour", (HANDLE)1);

	gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgComboDropdown);
	SetFontType(VaFontType::EnvironmentFont);

	auto cbctrl = GetComboBoxCtrl();
	if (cbctrl)
		m_hCombo = cbctrl->m_hWnd;
	else
		vLog("ERROR: CFastComboBox::Init GetComboBoxCtrl fail"); // [case: 110697]

	if (!m_lstCombo.m_hWnd)
	{
		CRect rc(0, 0, 100, 100);
#define LVS_EX_LABELTIP 0x00004000 // listview unfolds partly hidden labels if it does not have infotip text
		const DWORD exStyle = LVS_EX_LABELTIP | LVS_EX_ONECLICKACTIVATE | LVS_EX_TRACKSELECT | LVS_EX_FULLROWSELECT;
		const DWORD dwStyle = WS_CHILD | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
		                      LVS_SHOWSELALWAYS | LVS_OWNERDRAWFIXED | LVS_OWNERDATA | LVS_SHAREIMAGELISTS;
		// |LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER|LVS_REPORT|LVS_SHAREIMAGELISTS|LVS_SINGLESEL|LVS_OWNERDRAWFIXED|LVS_OWNERDATA
#if _MSC_VER <= 1200
		m_lstCombo.CreateEx(0, WC_LISTVIEW, NULL, dwStyle, rc, this,
		                    0); // owner must be this, so we get WM_MEASUREITEM notifications
#else
		m_lstCombo.CreateEx(0, dwStyle, rc, this, 0); // owner must be this, so we get WM_MEASUREITEM notifications
#endif
		const DWORD dwStyleEx = m_lstCombo.GetExtendedStyle();
		m_lstCombo.SetExtendedStyle(exStyle | dwStyleEx);
		m_lstCombo.Init(this, mHasColorableContent);

		if (!m_comboSub.m_hWnd)
			m_comboSub.Init(this);
		if (!m_edSub.m_hWnd)
			m_edSub.Init(this);

		CRect rcAll;
		GetDroppedControlRect(&rcAll);
		GetWindowRect(&rc);
		SetDroppedWidth((UINT)rcAll.Width());
		SetDroppedHeight(UINT(rcAll.Height() - rc.Height()));
	}

	if (!mHasColorableContent)
	{
		mySetProp(m_hCombo, "__VA_do_not_colour", (HANDLE)1);
		mySetProp(m_lstCombo.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
		mySetProp(m_comboSub.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
		mySetProp(m_edSub.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	}
}

#if _MSC_VER <= 1200
void CFastComboBox::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	CComboBoxEx::OnCreate(lpCreateStruct);
	Init();
}
#else
int CFastComboBox::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	int retval = CComboBoxEx::OnCreate(lpCreateStruct);
	Init();
	return retval;
}
#endif

BOOL CFastComboBox::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->hwnd == m_hCombo)
	{
		if (pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_LBUTTONUP)
		{
			SendMessage(pMsg->message, pMsg->wParam, pMsg->lParam);
			return TRUE;
		}
	}
	if (pMsg->message == WM_KEYDOWN &&
	    (pMsg->wParam == VK_DOWN || pMsg->wParam == VK_UP || pMsg->wParam == VK_NEXT || pMsg->wParam == VK_PRIOR))
	{
		DisplayList(TRUE);
		m_lstCombo.SendMessage(pMsg->message, pMsg->wParam, pMsg->lParam);
		return TRUE;
	}
	return CComboBoxEx::PreTranslateMessage(pMsg);
}

void CFastComboBox::SetItemCount(int nItems)
{
	m_lstCombo.SetItemCount(nItems);
}

void CFastComboBox::SetImageList(CImageList* pImageList, int nImageListType)
{
	m_lstCombo.SetImageList(pImageList, nImageListType);
}

int CFastComboBox::GetItemCount() const
{
	return m_lstCombo.GetItemCount();
}

POSITION
CFastComboBox::GetFirstSelectedItemPosition() const
{
	return m_lstCombo.GetFirstSelectedItemPosition();
}

int CFastComboBox::GetNextSelectedItem(POSITION& pos) const
{
	return m_lstCombo.GetNextSelectedItem(pos);
}

WTString CFastComboBox::GetItemText(int nItem, int nSubItem) const
{
	return GetItemTextW(nItem, nSubItem);
}

CStringW CFastComboBox::GetItemTextW(int nItem, int nSubItem) const
{
	LVITEMW lvi;
	memset(&lvi, 0, sizeof(LVITEMW));
	lvi.iSubItem = nSubItem;
	CStringW str;
	int nLen = 128;
	int nRes;
	do
	{
		nLen *= 2;
		lvi.cchTextMax = nLen;
		lvi.pszText = str.GetBufferSetLength(nLen);
		nRes = (int)::SendMessageW(m_lstCombo.m_hWnd, LVM_GETITEMTEXTW, (WPARAM)nItem, (LPARAM)&lvi);
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}

BOOL CFastComboBox::GetItem(LVITEM* pItem) const
{
	return m_lstCombo.GetItem(pItem);
}

BOOL CFastComboBox::SetItemState(int nItem, UINT nState, UINT nMask)
{
	return m_lstCombo.SetItemState(nItem, nState, nMask);
}

BOOL CFastComboBox::EnsureVisible(int nItem, BOOL bPartialOK)
{
	return m_lstCombo.EnsureVisible(nItem, bPartialOK);
}

BOOL CFastComboBox::IsWindow() const
{
	return ::IsWindow(m_lstCombo.m_hWnd);
}

int CFastComboBox::SetSelectionMark(int iIndex)
{
	return m_lstCombo.SetSelectionMark(iIndex);
}

int CFastComboBox::SetHotItem(int iIndex)
{
	return m_lstCombo.SetHotItem(iIndex);
}

LRESULT CFastComboBox::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (IsVS2010ColouringActive())
	{
		std::optional<LRESULT> ret = ComboVS2010_CComboBoxEx_WndProc(m_hWnd, message, wParam, lParam,
		                                                             GetVS2010ComboBackgroundColour(), bgbrush_cache);
		if (ret)
			return *ret;
	}

	return __super::WindowProc(message, wParam, lParam); // __super so we get messages working in base class
}

COLORREF CFastComboBox::GetVS2010ComboBackgroundColour() const
{
	return GetVS2010ComboColour(BACKGROUND);
}

bool CFastComboBox::ComboDrawsItsOwnBorder() const
{
	if (IsVS2010ColouringActive())
		return true;
	return false;
}

bool CFastComboBox::HasArrowButtonRightBorder() const
{
	return !IsVS2010ColouringActive();
}

void CFastComboBox::OnDpiChanged(DpiChange change, bool& handled)
{
	if (change == CDpiAware::DpiChange::AfterParent)
	{
		gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgComboDropdown);
		UpdateFonts(VAFTF_EnvironmentFont);	
	}

	__super::OnDpiChanged(change, handled);
}

void CFastComboBox::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	// DrawItem virtual function is no good; all items in comboboxex are customdrawn, so we don't have to put flag for
	// it
	if (IsVS2010ColouringActive())
	{
		if (ComboVS2010_CComboBoxEx_OnDrawItem(this, lpDrawItemStruct, GetVS2010ComboBackgroundColour(), true,
		                                       *popup_background_gradient_cache))
			return;
	}

	CComboBoxEx::OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CFastComboBox::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	CComboBoxEx::OnMeasureItem(nIDCtl, lpMeasureItemStruct);
	if (lpMeasureItemStruct)
		lpMeasureItemStruct->itemHeight = (UINT)MeasureItemHeight();
}

void CFastComboBox::OnHighlightStateChange(uint new_state) const
{
	::mySetProp(m_hWnd, "last_highlight_state", (HANDLE)(uintptr_t)new_state);
	if (highlight_state_change_event)
		highlight_state_change_event(new_state);
}

void CFastComboBox::SettingsChanged()
{
	if (!IsVS2010ColouringActive())
		return;

	popup_background_gradient_cache.reset(new CGradientCache);

	gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgComboDropdown);
	SetFontType(VaFontType::EnvironmentFont);

	if (gShellAttr->IsDevenv11OrHigher())
		bgbrush_cache.DeleteObject();
}
