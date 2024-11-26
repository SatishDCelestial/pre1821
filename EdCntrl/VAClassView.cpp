// VAClassView.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "VAClassView.h"
#include "WTString.h"
#include "EdCnt.h"
#include "expansion.h"
#include "VACompletionBox.h"
#include "VAWorkspaceViews.h"
#include "WorkSpaceTab.h"
#include "DevShellAttributes.h"
#include "VARefactor.h"
#include "MenuXP/Tools.h"
#include "Mparse.h"
#include "FileTypes.h"
#include "Settings.h"
#include "DBLock.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "VaService.h"
#include "FindReferences.h"
#include "RenameReferencesDlg.h"
#include "SyntaxColoring.h"
#include "project.h"
#include "VAAutomation.h"
#include "FeatureSupport.h"
#include "ParseThrd.h"
#include "InferType.h"
#include "IdeSettings.h"
#include "ImageListManager.h"
#include "FILE.H"
#include "..\common\TempAssign.h"
#include "LogElapsedTime.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "includesDb.h"
#include "FileId.h"
#include "SubClassWnd.h"
#include "FontSettings.h"
#include "RegKeys.h"

#ifdef _WIN64
#include "vsshell140.h"
#include "KnownMonikers.h"
using namespace Microsoft::VisualStudio::Imaging;
#endif // _WIN64

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define IDC_UPDATEWITHCARET 40074
#define IDC_AutoListIncludes 40075
#define IDC_UpdateHcbOnHover 40076
#define ROOT_NODE "root node"
#define ROOT_NODE_W L"root node"

WTString ClassViewSym;
CVAClassView* g_CVAClassView = NULL;

extern void ClearTextOutCacheComment();

CComPtr<IVsThreadedWaitDialog> GetVsThreadedWaitDialog()
{
	if (!gShellAttr->IsDevenv8OrHigher())
		return nullptr;

	CComQIPtr<IVsThreadedWaitDialog> vsDlg;
	if (!gPkgServiceProvider)
		return vsDlg;

	IUnknown* tmp = NULL;
	gPkgServiceProvider->QueryService(SID_SVsThreadedWaitDialog, IID_IVsThreadedWaitDialog, (void**)&tmp);
	if (!tmp)
		return vsDlg;

	vsDlg = tmp;
	return vsDlg;
}

class TreeSubClass : public ToolTipWrapper<CColorVS2010TreeCtrl>
{
	CToolTipCtrl* mTips;
	bool mColorableContent;
	std::unique_ptr<CGradientCache> background_gradient_cache;
	CPoint mMousePos, mKeyPos;

	// backing store of DTypes referenced by tree node items to remove dependency
	// on lifetime of DTypes that are used to populate the tree
	std::vector<std::shared_ptr<DType>> mItemData;

	DECLARE_MESSAGE_MAP()

  protected:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case TVM_INSERTITEMW: {
			TVINSERTSTRUCTW* lp = reinterpret_cast<TVINSERTSTRUCTW*>(lParam);
			if (lp && lp->item.lParam)
			{
				std::shared_ptr<DType> dat(new DType(reinterpret_cast<DType*>(lp->item.lParam)));
				mItemData.push_back(dat);
				lp->item.lParam = reinterpret_cast<LPARAM>(dat.get());
			}
		}
		break;
		case TVM_INSERTITEMA: {
			TVINSERTSTRUCTA* lp = reinterpret_cast<TVINSERTSTRUCTA*>(lParam);
			if (lp && lp->item.lParam)
			{
				std::shared_ptr<DType> dat(new DType(reinterpret_cast<DType*>(lp->item.lParam)));
				mItemData.push_back(dat);
				lp->item.lParam = reinterpret_cast<LPARAM>(dat.get());
			}
		}
		break;
		case WM_DESTROY:
			KillTimer(1);
			break;
		case WM_MOUSEMOVE: {
			CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			mMousePos = pt;
		}
		break;
		case WM_TIMER:
			if (wParam == 1)
			{
				KillTimer(1);
				if (mTips && mTips->GetSafeHwnd() && mTips->IsWindowVisible())
				{
					CPoint pt;
					::GetCursorPos(&pt);
					if (::WindowFromPoint(pt) == GetSafeHwnd())
						SetTimer(1, 100, NULL);
					else
						mTips->ShowWindow(SW_HIDE);
				}
			}
			break;
		case WM_HSCROLL:
		case WM_VSCROLL:
			if (GetFocus() != this)
				SetFocus();
			break;
		case WM_ERASEBKGND:
			if (IsVS2010VAViewColouringActive())
				return 1;
			return FALSE; // flicker when tooltips redraw
		case WM_WINDOWPOSCHANGED:
			UpdateTooltipRect();
			break;
		case WM_LBUTTONDOWN:
			VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftHcb);
			break;
		case WM_KEYDOWN:
		case WM_KEYUP:
			if (wParam == VK_MULTIPLY) // case 80227
				return TRUE;
			if (mTips->GetSafeHwnd() && mTips->IsWindowVisible())
				mTips->ShowWindow(SW_HIDE);

			{
				DWORD p = GetMessagePos();
				CPoint pt(GET_X_LPARAM(p), GET_Y_LPARAM(p));
				mMousePos = mKeyPos = pt;
			}

			if (WM_KEYDOWN == message)
			{
				if (gShellAttr->IsMsdev() && (wParam == VK_NEXT || wParam == VK_PRIOR) &&
				    (GetKeyState(VK_CONTROL) & 0x1000))
				{
					_ASSERTE(g_WorkSpaceTab);
					g_WorkSpaceTab->SwitchTab(wParam == VK_NEXT);
					return TRUE;
				}
			}
			break;
		}

		if (message == WM_PAINT)
		{
			VAColorPaintMessages w(mColorableContent ? PaintType::View : PaintType::DontColor);
			return ToolTipWrapper<CColorVS2010TreeCtrl>::WindowProc(message, wParam, lParam);
		}

		LRESULT r = ToolTipWrapper<CColorVS2010TreeCtrl>::WindowProc(message, wParam, lParam);
		return r;
	}

  public:
	TreeSubClass(CTreeCtrl& tree) : ToolTipWrapper(NULL, TT_TEXT, 22, FALSE), mTips(NULL), mColorableContent(true)
	{
		SubclassWindow(tree.m_hWnd);
		mTips = tree.GetToolTips();
		if (mTips)
		{
			mTips->SetWindowPos(&wndTopMost, 0, 0, 10, 10, SWP_NOACTIVATE);
			mTips->SetDelayTime(TTDT_AUTOPOP, 30 * 60 * 1000);
		}
		else
			_ASSERTE(!"no ToolTipCtrl for VAView tree");
	}

	~TreeSubClass()
	{
		mTips = NULL;
		if (m_hWnd)
		{
			KillTimer(1);
			UnsubclassWindow();
		}
	}

	afx_msg BOOL OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* result)
	{
		CPoint mouse((LPARAM)::GetMessagePos());
		ScreenToClient(&mouse);
		HTREEITEM itemUnderMouse = HitTest(mouse);
		if (itemUnderMouse != nullptr)
		{
			if (g_FontSettings)
				((CToolTipCtrl*)CWnd::FromHandle(pNMHDR->hwndFrom))
				    ->SetMaxTipWidth(g_FontSettings->m_tooltipWidth - 100);
			static wchar_t ttbuffer[4096];
			ttbuffer[0] = 0;
			TOOLTIPTEXTW* tttw = (TOOLTIPTEXTW*)pNMHDR;
			tttw->lpszText = ttbuffer;
			NMTVGETINFOTIPW git;
			memset(&git, 0, sizeof(git));
			git.hdr.code = TVN_GETINFOTIPW;
			git.hdr.hwndFrom = *this;
			git.hdr.idFrom = (uint)GetDlgCtrlID();
			git.hItem = itemUnderMouse;
			git.cchTextMax = sizeof(ttbuffer) / sizeof(ttbuffer[0]) - 1;
			git.pszText = ttbuffer;
			GetParent()->SendMessage(WM_NOTIFY, (WPARAM)GetDlgCtrlID(), (LPARAM)&git);
			if (result)
				*result = NULL;
		}
		return itemUnderMouse != nullptr ? TRUE : FALSE;
	}

	void UpdateTooltipRect()
	{
		auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(this);

		CRect rc;
		GetWindowRect(&rc);
		// case 518: prevent display of default tooltip when mouse is over scrollbars
		if (GetStyle() & WS_VSCROLL)
			rc.right -= VsUI::DpiHelper::GetSystemMetrics(SM_CXVSCROLL);
		if (GetStyle() & WS_HSCROLL)
			rc.bottom -= VsUI::DpiHelper::GetSystemMetrics(SM_CYHSCROLL);
		SetToolRect(rc);
	}

	afx_msg void OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
	{
		if (IsVS2010VAViewColouringActive())
			TreeVS2010CustomDraw(*this, this, pNMHDR, pResult, background_gradient_cache);
		else
			*pResult = CDRF_DODEFAULT;
	}

	void ClearAll()
	{
		DeleteAllItems();
		mItemData.clear();
	}

	void SetColorable(bool c)
	{
		mColorableContent = c;
	}

	BOOL AllowTooltip() const
	{
		return mKeyPos != mMousePos;
	}
};

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(TreeSubClass, ToolTipWrapper<CColorVS2010TreeCtrl>)
ON_WM_ERASEBKGND()
ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnTreeCustomDraw)
ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CVAClassView dialog

static int CALLBACK MyCompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	// lParamSort contains a pointer to the tree control.
	// The lParam of an item is just its handle,
	// as specified with SetItemData
	DType* obj1 = (DType*)lParam1;
	DType* obj2 = (DType*)lParam2;
	if (!obj1)
		return 1;
	if (!obj2)
		return -1;
	uint t1 = obj1->MaskedType();
	uint t2 = obj2->MaskedType();
	if (t1 != t2)
		return int(t1 - t2);
	try
	{
		return strcmp(obj1->SymScope().c_str(), obj2->SymScope().c_str());
	}
	catch (...)
	{
		// obj has been freed
		VALOGEXCEPTION("VACV:");
	}
	return FALSE;
}

CVAClassView::CVAClassView(CWnd* pParent /*=NULL*/)
    : VADialog(UINT(gShellAttr->IsDevenv11OrHigher() ? IDD_VACLASSVIEWv11 : IDD_VACLASSVIEW), pParent, fdAll,
               UINT(flAntiFlicker | flNoSavePos)),
      m_fileView(TRUE), mTheSameTreeCtrl(NULL), mFileIdOfDependentSym(0), mIsExpanding(false), mIncludeEdLine(-1),
      mExpandAllMode(false)
{
	g_CVAClassView = this;
	//{{AFX_DATA_INIT(CVAClassView)
	//}}AFX_DATA_INIT
}

CVAClassView::~CVAClassView()
{
	g_CVAClassView = NULL;
	delete mTheSameTreeCtrl;
}

void CVAClassView::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CVAClassView)
	//}}AFX_DATA_MAP
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CVAClassView, VADialog)
//{{AFX_MSG_MAP(CVAClassView)
ON_WM_CONTEXTMENU()
ON_WM_SIZE()
ON_WM_DESTROY()
ON_NOTIFY(NM_DBLCLK, IDC_TREE1, OnHcbDoubleClick)
ON_NOTIFY(NM_RETURN, IDC_TREE1, OnHcbDoubleClick)
ON_WM_CREATE()
ON_NOTIFY(NM_RCLICK, IDC_TREE1, OnHcbRightClick)
ON_BN_CLICKED(IDC_BUTTON1, OnToggleSymbolLock)
ON_NOTIFY(TVN_ITEMEXPANDING, IDC_TREE1, OnHcbItemExpanding)
ON_WM_DRAWITEM()
//}}AFX_MSG_MAP
ON_NOTIFY(TVN_SELCHANGED, IDC_TREE1, OnHcbTvnSelectionChanged)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CVAClassView message handlers

LRESULT
CVAClassView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_ERASEBKGND && CVS2010Colours::IsVS2010VAViewColouringActive())
	{
		COLORREF clr = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", false);
		if (UINT_MAX != clr)
		{
			CRect crect;
			GetClientRect(&crect);
			CBrush brush;
			if (brush.CreateSolidBrush(clr))
			{
				::FillRect((HDC)wParam, crect, brush);
	
				if (::IsWindow(m_titlebox.m_hWnd))
					m_titlebox.RedrawWindow();
				
				return true;
			}
		}
	}

	auto result = __super::WindowProc(message, wParam, lParam);

	if (message == WM_WINDOWPOSCHANGED && CVS2010Colours::IsVS2010VAViewColouringActive())
	{
		if (::IsWindow(m_titlebox.m_hWnd))
			m_titlebox.RedrawWindow();
	}

	return result;
}

void CVAClassView::OnDpiChanged(DpiChange change, bool& handled)
{
	__super::OnDpiChanged(change, handled);

	if (change == DpiChange::AfterParent)
	{
		// Update layout

		CButton* btn = (CButton*)GetDlgItem(IDC_BUTTON1);
		CRect titleboxRc;
		m_titlebox.GetWindowRect(&titleboxRc);
		CRect btnRc;
		btn->GetWindowRect(&btnRc);
		const int w = btnRc.Width();
		btnRc.right = titleboxRc.right - 1;
		btnRc.left = btnRc.right - w;
		btnRc.top = titleboxRc.top + 1;
		btnRc.bottom = titleboxRc.bottom - 1;
		ScreenToClient(btnRc);
		btn->MoveWindow(btnRc);

		CRect treeRc;
		m_tree.GetWindowRect(&treeRc);
		treeRc.top = titleboxRc.bottom;
		ScreenToClient(treeRc);

		if (!gShellAttr->IsMsdev())
			treeRc.bottom++;

		m_tree.MoveWindow(treeRc);

		ResetLayout();
	}
}

void CVAClassView::SetTitle(LPCSTR title, int img /* = 0 */)
{
	WTString txt;

	if (img == -1)
		txt = title;
	else
	{
		txt = DecodeScope(title);
		WTString sym = StrGetSym(txt);
		if (sym.Find('>') == -1)
			txt = sym;
	}

	CStringW wTxt = txt.Wide();

	COMBOBOXEXITEMW cbi;
	ZeroMemory(&cbi, sizeof(COMBOBOXEXITEMW));
	cbi.iItem = 0;
	cbi.pszText = (LPWSTR)(LPCWSTR)wTxt;
	cbi.cchTextMax = wTxt.GetLength();
	cbi.mask = CBEIF_IMAGE | CBEIF_INDENT | CBEIF_OVERLAY | CBEIF_SELECTEDIMAGE | CBEIF_TEXT;

	if (!wTxt.GetLength())
		cbi.iImage = -1;
	else if (m_fileView)
		cbi.iImage = GetFileImgIdx(CStringW(WTString(title).Wide()), ICONIDX_SCOPECUR);
	else
		cbi.iImage = img;

	cbi.iSelectedImage = cbi.iImage;
	cbi.iOverlay = 8;
	cbi.iIndent = 0; // Set indentation according

	auto func = [this, &cbi] {
		m_titlebox.DeleteItem(0);
		m_titlebox.InsertItem(&cbi);
		m_titlebox.SetCurSel(0);
		CEdit* ed(m_titlebox.GetEditCtrl());
		if (ed)
			ed->RedrawWindow();
	};

	// [case: 89977]
	if (::GetCurrentThreadId() == g_mainThread)
		func();
	else
		::RunFromMainThread(func);
}

void CVAClassView::OnSize(UINT nType, int cx, int cy)
{
	__super::OnSize(nType, cx, cy);
}

void CVAClassView::OnHcbDoubleClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 1;
	GotoItemSymbol(m_tree.GetSelectedItem());
}

int CVAClassView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (__super::OnCreate(lpCreateStruct) == -1)
		return -1;

	return 0;
}

BOOL CVAClassView::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	NMHDR* pNMHDR = (NMHDR*)lParam;
	//	if (pNMHDR->code == EN_CHANGE)
	//	{
	//		int n = 123;
	//	}
	if (pNMHDR->code == TVN_GETINFOTIPA)
	{
		LPNMTVGETINFOTIP lpGetInfoTipw = (LPNMTVGETINFOTIP)lParam;
		HTREEITEM i = lpGetInfoTipw->hItem;
		const WTString txt(GetTooltipText(i));

		for (int n = 0; n < lpGetInfoTipw->cchTextMax && txt[n]; n++)
		{
			lpGetInfoTipw->pszText[n] = txt[n];
			lpGetInfoTipw->pszText[n + 1] = '\0';
		}

		ClearTextOutCacheComment(); // [case: 16082]
		m_tree.SetTimer(1, 100, NULL);
		return TRUE;
	}
	if (pNMHDR->code == TVN_GETINFOTIPW)
	{
		LPNMTVGETINFOTIPW lpGetInfoTipw = (LPNMTVGETINFOTIPW)lParam;
		HTREEITEM i = lpGetInfoTipw->hItem;
		const WTString txt(GetTooltipText(i));

		int len = MultiByteToWideChar(CP_UTF8, 0, txt.c_str(), txt.GetLength(), lpGetInfoTipw->pszText,
		                              lpGetInfoTipw->cchTextMax);
		lpGetInfoTipw->pszText[len] = L'\0';

		ClearTextOutCacheComment(); // [case: 16082]
		m_tree.SetTimer(1, 100, NULL);
		return TRUE;
	}

	if (pNMHDR->code == TVN_GETDISPINFO)
	{
		LPNMTVDISPINFO lpGetInfo = (LPNMTVDISPINFO)lParam;
		if (lpGetInfo->item.mask & TVIF_CHILDREN)
		{
			// notification for I_CHILDRENCALLBACK
			HTREEITEM i = lpGetInfo->item.hItem;
			DType* dt = (DType*)m_tree.GetItemData(i);
			if (dt && dt->MaskedType() == vaInclude)
			{
				const CStringW curFile(gFileIdManager->GetFile(dt->Def()));
				if (IncludesDb::HasIncludes(curFile, DTypeDbScope::dbSystemIfNoSln))
					lpGetInfo->item.cChildren = 1;
				else
					lpGetInfo->item.cChildren = 0;
				return TRUE;
			}
			else if (dt && dt->MaskedType() == vaIncludeBy)
			{
				const CStringW curFile(gFileIdManager->GetFile(dt->FileId()));
				if (IncludesDb::HasIncludedBys(curFile, DTypeDbScope::dbSystemIfNoSln))
					lpGetInfo->item.cChildren = 1;
				else
					lpGetInfo->item.cChildren = 0;
				return TRUE;
			}
		}
	}

	return __super::OnNotify(wParam, lParam, pResult);
}

BOOL CVAClassView::OnInitDialog()
{
	__super::OnInitDialog();

	CEdit* pTree = (CEdit*)GetDlgItem(IDC_TREE1);
	m_tree.m_hWnd = pTree->m_hWnd;

	m_tree.ModifyStyle(TVS_FULLROWSELECT, TVS_NONEVENHEIGHT | TVS_HASBUTTONS | TVS_HASLINES | TVS_SHOWSELALWAYS |
	                                          TVS_INFOTIP | WS_TABSTOP /*|TVS_LINESATROOT*/);
	if (gShellAttr->IsMsdev())
	{
		// if you leave style as WS_EX_CLIENTEDGE, make sure to remove
		// the condition before 'treeRc.bottom++'.
		// WS_EX_CLIENTEDGE leaves a bold edge on the left that is
		// more noticeable than the soft edge on the bottom and right
		// with WS_EX_STATICEDGE.
		m_tree.ModifyStyleEx(WS_EX_CLIENTEDGE, WS_EX_STATICEDGE);
	}
	else if (CVS2010Colours::IsVS2010VAViewColouringActive())
	{
		m_tree.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		m_tree.ModifyStyle(0, TVS_FULLROWSELECT);
	}
	else if (::GetWinVersion() < wvWinXP || gShellAttr->IsDevenv7())
	{
		// <= vs2003 in xp or vs200x in win2000
		m_tree.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		m_tree.ModifyStyle(0, WS_BORDER);
	}

	gImgListMgr->SetImgListForDPI(m_tree, ImageListManager::bgTree, TVSIL_NORMAL);
	CRect r;
	GetClientRect(&r);
	r.bottom = 34;
	const uint clipstyle =
	    CVS2010Colours::IsVS2010VAViewColouringActive() ? UINT(WS_CLIPCHILDREN | WS_CLIPSIBLINGS) : WS_CLIPSIBLINGS;
	m_titlebox.Create(WS_VISIBLE | CBS_DROPDOWN | WS_TABSTOP | clipstyle, r, this, IDC_LIST1);
	m_titlebox.Init(NULL);

	CButton* btn = (CButton*)GetDlgItem(IDC_BUTTON1);
	if (CVS2010Colours::IsVS2010VAViewColouringActive())
		btn->ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
	CRect titleboxRc;
	m_titlebox.GetWindowRect(&titleboxRc);
	CRect btnRc;
	btn->GetWindowRect(&btnRc);
	const int w = btnRc.Width();
	btnRc.right = titleboxRc.right - 1;
	btnRc.left = btnRc.right - w;
	btnRc.top = titleboxRc.top + 1;
	btnRc.bottom = titleboxRc.bottom - 1;
	ScreenToClient(btnRc);
	btn->MoveWindow(btnRc);

	CRect treeRc;
	m_tree.GetWindowRect(&treeRc);
	treeRc.top = titleboxRc.bottom;
	if (!gShellAttr->IsMsdev())
		treeRc.bottom++;
	ScreenToClient(treeRc);
	m_tree.MoveWindow(treeRc);

	AddSzControl(IDC_TREE1, mdResize, mdResize);
	AddSzControl(IDC_BUTTON1, mdRepos, mdNone);
	AddSzControl(m_titlebox, mdResize, mdNone);

	new ToolTipWrapper<>(btn->m_hWnd, "Keep or update content of Hovering Class Browser.", 22);
	::SetWindowPos(m_titlebox.GetSafeHwnd(), HWND_TOP, 0, 0, 0, 0,
	               SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER);
	::SetWindowPos(btn->GetSafeHwnd(), HWND_TOP, 0, 0, 0, 0,
	               SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER);

	mTheSameTreeCtrl = new TreeSubClass(m_tree);

	m_lock = TRUE; // OnButton1 will toggle off
	OnToggleSymbolLock();
	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

HTREEITEM
CVAClassView::GetItemFromPos(POINT pos) const
{
	TVHITTESTINFO ht = {{0}};
	ht.pt = pos;
	::MapWindowPoints(HWND_DESKTOP, m_tree.m_hWnd, &ht.pt, 1);

	if (m_tree.HitTest(&ht) && (ht.flags & TVHT_ONITEM))
		return ht.hItem;

	return NULL;
}

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

void CVAClassView::OnHcbRightClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftHcb);
	*pResult = 0; // let the tree send us a WM_CONTEXTMENU msg

	const CPoint pt(GetCurrentMessage()->pt);
	HTREEITEM it = GetItemFromPos(pt);
	if (it)
		m_tree.SelectItem(it);
}

void CVAClassView::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftHcb);
	HTREEITEM selItem = m_tree.GetSelectedItem();

	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && selItem)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (m_tree.GetItemRect(selItem, &rc, TRUE))
		{
			m_tree.ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}

	DType* data = GetSelItemClassData();
	DisplayContextMenu(pos, data);
}

void CVAClassView::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT dis)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if ((nIDCtl != IDC_BUTTON1) || !CVS2010Colours::IsVS2010VAViewColouringActive())
		return __super::OnDrawItem(nIDCtl, dis);

	COLORREF bgclr;
	if (dis->itemState & ODS_SELECTED)
		bgclr = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseDownBackgroundBegin", false);
	else if (dis->itemState & ODS_HOTLIGHT)
		bgclr = g_IdeSettings->GetEnvironmentColor(L"CommandBarMouseOverBackgroundBegin", false);
	else
		bgclr = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", false);

	if (UINT_MAX == bgclr)
		return __super::OnDrawItem(nIDCtl, dis);

	int savedc = ::SaveDC(dis->hDC);
	CBrush brush;
	brush.CreateSolidBrush(bgclr);
	::FillRect(dis->hDC, &dis->rcItem, brush);

	CSize iconsize(VsUI::DpiHelper::LogicalToDeviceUnitsX(16), VsUI::DpiHelper::LogicalToDeviceUnitsY(16));

#ifdef _WIN64
	CBitmap monImg;
	auto mon = m_lock ? KnownMonikers::Pin : KnownMonikers::Unpin;
	if (SUCCEEDED(ImageListManager::GetMonikerImage(monImg, mon, bgclr, 0)) && monImg.m_hObject)
	{
		CRect rcIcon(0, 0, iconsize.cx, iconsize.cy);
		ThemeUtils::Rect_CenterAlign(&dis->rcItem, &rcIcon);
		ThemeUtils::DrawImage(dis->hDC, monImg, rcIcon.TopLeft());
	}	
#else
	HICON icon;

	if (m_lock)
		icon = ::LoadIcon(::AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_PUSHPINLOCK11));
	else
		icon = ::LoadIcon(::AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_PUSHPINUNLOCK11));

	::DrawIconEx(dis->hDC, dis->rcItem.left + (dis->rcItem.right - dis->rcItem.left - iconsize.cx) / 2,
		            dis->rcItem.top + (dis->rcItem.bottom - dis->rcItem.top - iconsize.cy) / 2, icon, iconsize.cx,
		            iconsize.cy, 0, nullptr, DI_NORMAL);

	::DestroyIcon(icon);	
#endif
	::RestoreDC(dis->hDC, savedc);
}

DType* CVAClassView::GetSelItemClassData()
{
	DType* data = nullptr;
	HTREEITEM selItem = m_tree.GetSelectedItem();
	if (selItem)
	{
		data = (DType*)m_tree.GetItemData(selItem);
		if (!data)
		{
			EdCntPtr curEd = g_currentEdCnt;
			if (curEd)
			{
				const WTString itxt(GetTreeItemText(m_tree.GetSafeHwnd(), selItem));
				MultiParsePtr mp(curEd->GetParseDb());
				data = mp->FindExact(itxt);
			}
		}
	}
	return data;
}

void CVAClassView::EditorClicked()
{
	if (Psettings->m_nHCBOptions & 2)
		GetOutline(FALSE);
}

void CVAClassView::GetOutline(BOOL force)
{
	if (m_lock)
		return;

	EdCntPtr curEd = g_currentEdCnt;
	if (curEd)
	{
		static uint lpos = 0;
		uint cpos = curEd->CurPos();
		const bool kPosChange = cpos != lpos;
		if (kPosChange)
			m_fileView = TRUE;
		lpos = cpos;

		if (Psettings->m_nHCBOptions & 2 && kPosChange && !force)
		{
			// [case: 39883] this block is based on EdCnt::DisplayClassInfo
			if (IsFeatureSupported(Feature_HCB, curEd->m_ScopeLangType) && CAN_USE_NEW_SCOPE())
			{
				MultiParsePtr mp = curEd->GetParseDb();
				if (!(mp->GetFilename().IsEmpty() || GlobalProject->IsBusy() ||
				      (g_ParserThread && g_ParserThread->IsNormalJobActiveOrPending())))
				{
					const WTString curBuf(curEd->GetBuf());
					if (!curBuf.IsEmpty())
					{
						WTString scope;
						DTypePtr data = ::SymFromPos(curBuf, mp, curEd->GetBufIndex(curBuf, (long)cpos), scope, false);
						if (data && data->MaskedType() != RESWORD)
						{
							const WTString newSym(data->SymScope());
							if (newSym.GetLength())
							{
								if (m_lastSym == newSym && ClassViewSym == newSym)
								{
									// [case: 76918]
									return;
								}

								ClassViewSym = newSym;
								::QueueHcbRefresh();
								return;
							}
						}
					}
				}
			}
		}

		if (Psettings->m_nHCBOptions & 0x1)
		{
			if (m_fileView || force)
			{
				WTString scope = curEd->m_lastScope;
				// [case: 80008]
				::ScopeToSymbolScope(scope);
				MultiParsePtr mp(curEd->GetParseDb());
				DB_READ_LOCK;
				while (scope.GetLength())
				{
					DType* data = mp->FindExact(scope);
					if (data)
					{
						WTString dataSymScope(data->SymScope());
						if (dataSymScope.GetLength() &&
						    _tcsnicmp(scope.c_str(), dataSymScope.c_str(), (uint)dataSymScope.GetLength()) == 0)
						{
							ClassViewSym = scope;
							m_fileView = TRUE;
							::QueueHcbRefresh();
							return;
						}
					}
					scope = StrGetSymScope(scope);
				}
			}
		}

		if (Psettings->mAutoListIncludes && IsFileWithIncludes(curEd->m_ftype))
		{
			int ln = curEd->CurLine();
			if (TERISRC(ln))
				ln = (int)TERROW(ln);
			if (ln < 15 && ln >= 0)
			{
				// [case: 7156]
				// automatically display includes if within first 10 lines and we have nothing else
				// to display
				::QueueHcbIncludeRefresh(true);
				return;
			}
		}
	}

	if (gTestsActive)
		mInfoForLogging.WTFormat("Outline empty");
}

void CVAClassView::ClearHcb(bool clearTree /*= true*/)
{
	try
	{
		if (GetCurrentThreadId() != g_mainThread)
		{
			::RunFromMainThread([clearTree, this]() { if (::IsWindow(m_hWnd)) { ClearHcb(clearTree); } });
			return;
		}

		if (clearTree && mTheSameTreeCtrl)
		{
			m_tree.SetRedraw(FALSE);
			mTheSameTreeCtrl->ClearAll();
			SetTitle("");
			m_tree.SetRedraw(TRUE);
			m_tree.Invalidate(FALSE);
		}

		m_lastSym.Empty();
		mFileIdOfDependentSym = 0;
		mInfoForLogging.Empty();
		mIncludeEdLine = -1;
		mIncludeCurFile.Empty();
		mRootIncludeList.clear();
		mRootIncludeByList.clear();
	}
	catch (...)
	{
		VALOGEXCEPTION("VACV:");
	}
}

void RefreshHcb(bool force /*= false*/)
{
	if (g_CVAClassView && g_CVAClassView->m_hWnd && IsWindow(g_CVAClassView->m_hWnd))
	{
		g_CVAClassView->m_fileView = FALSE;
		if (force)
			g_CVAClassView->m_lastSym.Empty();
		g_CVAClassView->UpdateHcb();
	}
}

class RefreshHcbCls : public ParseWorkItem
{
	bool mForce;

  public:
	RefreshHcbCls(bool force = false) : ParseWorkItem("RefreshHcb")
	{
	}

	virtual void DoParseWork()
	{
		if (!StopIt)
			RefreshHcb(mForce);
	}
};

void QueueHcbRefresh()
{
	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new RefreshHcbCls());
}

void QueueHcbIncludeRefresh(bool force, bool immediate /*= false*/)
{
	if (!g_ParserThread || !GlobalProject || GlobalProject->IsBusy())
		return;

	EdCntPtr curEd = g_currentEdCnt;
	if (!curEd)
		return;

	const uint curFileId = gFileIdManager->GetFileId(curEd->FileName());
	ClassViewSym = FileIdManager::GetIncludeSymStr(curFileId);
	if (immediate)
		RefreshHcb(force);
	else
		g_ParserThread->QueueParseWorkItem(new RefreshHcbCls(force));
}

void CVAClassView::UpdateHcb()
{
	Log2(("updatehcb called for symbol " + ClassViewSym).c_str());

	if (m_lock)
		return;

	if (GetCurrentThreadId() != g_mainThread)
	{
		::RunFromMainThread([this]() { if (::IsWindow(m_hWnd)) { UpdateHcb(); } });
		return;
	}

	try
	{
		DB_READ_LOCK;
		if (m_lastSym == ClassViewSym)
			return;

		mFileIdOfDependentSym = 0;
		bool listIncludes = false;
		EdCntPtr curEd = g_currentEdCnt;
		if (!curEd || ClassViewSym.IsEmpty() || ClassViewSym[0] != ':' || ClassViewSym.Find(":VAunderline") == 0)
		{
			if (curEd && ClassViewSym.IsEmpty())
			{
				ClassViewSym = curEd->m_lastScope;
				if (curEd->m_lastScope == DB_SEP_STR)
				{
					ClearHcb();
					return;
				}
			}
			else if (curEd && 0 == ClassViewSym.Find("+ic:"))
			{
				listIncludes = true;
			}
			else
			{
				ClearHcb();
				return;
			}
		}

		bool didLock = false;
		if (gShellAttr->IsDevenv11OrHigher() && m_tree.GetStyle() & (WS_VSCROLL | WS_HSCROLL))
		{
			// [case: 67430] fix scrollbar flicker when contents change.
			// workaround is only necessary if scrollbar is present with current contents.
			didLock = true;
			SetRedraw(FALSE);
			m_tree.SetRedraw(FALSE);
			m_titlebox.SetRedraw(FALSE);
		}

		m_lastSym = ClassViewSym;
		bool doRedraw = true;
		if (listIncludes)
		{
			if (IsFileWithIncludes(curEd->m_ftype))
			{
				m_tree.ModifyStyle(0, TVS_LINESATROOT);
				const CStringW curFile(curEd->FileName());
				doRedraw = ListIncludes(TVI_ROOT, curFile, didLock);
			}
			else
				ClearHcb();
		}
		else
		{
			::myRemoveProp(m_tree.m_hWnd, "__VA_do_not_colour");
			mTheSameTreeCtrl->SetColorable(true);
			WTString sym = StrGetSym(ClassViewSym);
			WTString scope = StrGetSymScope(ClassViewSym);

			MultiParsePtr mp(curEd->GetParseDb());
			DType* dataScope = mp->FindExact(scope);
			DTypePtr dataBak;
			DType* data = mp->FindExact(ClassViewSym);
			//			int img = data?GetTypeImgIdx(data->MaskedType(), data->Attributes()):0;
			if (data)
			{
				switch (data->MaskedType())
				{
				case LINQ_VAR:
					// [case: 65344]
					dataBak = std::make_shared<DType>(data);
					dataBak = ::InferTypeFromAutoVar(dataBak, mp, curEd->m_ScopeLangType);
					data = dataBak.get();
				case VAR: {
					if (dataScope && dataScope->IsType())
						break;
					token t = GetTypesFromDef(data, (int)data->MaskedType(), curEd->m_ScopeLangType);
					ClassViewSym = t.read("\f");
				}
				case MAP:    // added for local typedefs
				case C_ENUM: // added for local typedefs
				case C_ENUMITEM:
				case C_INTERFACE: // added for local typedefs
				case NAMESPACE:   // added for local typedefs
				case STRUCT:      // added for local typedefs
				case CLASS:       // added for local typedefs
				case TYPE:
				case TEMPLATETYPE:
					if (/*dataScope &&*/ data->Def().GetLength() > 2)
					{
						scope = ClassViewSym;
						token bcl = mp->GetBaseClassList(scope);
						if (bcl.length())
						{
							scope = bcl.read("\f");
							dataScope = mp->FindExact(scope);
						}

						if (data->MaskedType() == C_ENUMITEM)
						{
							if (scope.IsEmpty())
							{
								scope = ::GetUnnamedParentScope(WTString(ClassViewSym), "enum");
								if (!scope.IsEmpty())
									dataScope = mp->FindExact(scope);
							}
						}
						else
							sym.Empty();
					}
					break;
				}
			}

			if (scope.length() && scope != ":wILDCard" && scope != ":")
				HierarchicalBrowse(scope, data, dataScope, sym);
			else if (!ClassViewSym.IsEmpty() && ClassViewSym != DB_SEP_CHR)
				FlatBrowse();
			else
				ClearHcb();
		}

		if (didLock)
		{
			SetRedraw(TRUE);
			m_tree.SetRedraw(TRUE);
			m_titlebox.SetRedraw(TRUE);

			if (doRedraw)
			{
				m_titlebox.RedrawWindow();
				m_tree.RedrawWindow();

				// this call updates the scrollbar (without this call,
				// scrollbar thumb is not drawn at correct position).
				m_tree.SetWindowPos(NULL, 0, 0, 0, 0,
				                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED |
				                        SWP_NOREPOSITION);
			}
			else
			{
				HTREEITEM it = m_tree.GetSelectedItem();
				if (it)
					m_tree.EnsureVisible(it);
			}
		}
		else if (doRedraw)
		{
			m_tree.RedrawWindow();
		}

		m_titlebox.SettingsChanged();

		// This non-sense is to prevent asserts from happening during
		// UpdateData when called from threads other than the main thread.
		CDialog* pThis = static_cast<CDialog*>(CWnd::FromHandle(m_hWnd));
		// if pThis == this then we are on the main thread.
		// if pThis != this, then different thread (ProjectLoader thread for example)
		if (pThis)
			pThis->UpdateData(TRUE);
	}
	catch (...)
	{
		VALOGEXCEPTION("VACV:");
		GetOutline(TRUE);
	}

	mTheSameTreeCtrl->UpdateTooltipRect();
}

void CVAClassView::OnToggleSymbolLock()
{
	m_lock = !m_lock;
	if (!m_lock)
		CheckHcbForDependentData();

	// unused in VS11!
	CButton* btn = (CButton*)GetDlgItem(IDC_BUTTON1);
	if (m_lock)
		btn->SetIcon(LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_PUSHPINLOCK)));
	else
		btn->SetIcon(LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_PUSHPINUNLOCK)));
}

void CVAClassView::OnHcbItemExpanding(NMHDR* pNMHDR, LRESULT* pResult)
{
	NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
	*pResult = 0;
	if (pNMTreeView->action != 2 || mIsExpanding)
		return;

	HTREEITEM hclass = pNMTreeView->itemNew.hItem;
	DType* obj = (DType*)m_tree.GetItemData(hclass);
	if (obj)
	{
		if (obj->MaskedType() == vaInclude || obj->MaskedType() == vaIncludeBy)
		{
			if (obj->Def() != ROOT_NODE)
			{
				*pResult = 1;
				if (hclass != m_tree.GetSelectedItem())
					m_tree.SelectItem(hclass);
				ExpandItemIncludes();
			}
			return;
		}
	}

	HTREEITEM ch = m_tree.GetChildItem(hclass);
	while (ch)
	{
		m_tree.DeleteItem(ch);
		ch = m_tree.GetChildItem(hclass);
	}
	WTString scope = GetTreeItemText(m_tree.GetSafeHwnd(), pNMTreeView->itemNew.hItem);
	if (scope.IsEmpty())
	{
		VALOGERROR("VACVExpand: empty scope");
		*pResult = 0;
		return;
	}

	EncodeTemplates(scope); // EncodeScope(scope);
	scope.Replace(".", ":");
	if (scope.GetLength() && scope[0] != L':')
		scope = WTString(':') + scope;
	EdCntPtr curEd = g_currentEdCnt;
	const bool candidateForLocal = curEd && (curEd->m_ftype == Src || curEd->m_ftype == UC);
	MultiParsePtr mp = curEd ? curEd->GetParseDb() : nullptr;
	bool doGlobalLookup = true;
	DType* scopeDat = nullptr;
	if (obj)
		scope = obj->SymScope().c_str();
	else if (candidateForLocal && mp)
	{
		scopeDat = mp->FindExact2(scope);
		if (scopeDat)
		{
			const uint type = scopeDat->MaskedType();
			if (type &&
			    (CLASS == type || STRUCT == type || C_ENUM == type || C_ENUMITEM == type || C_INTERFACE == type))
			{
				// [case: 66202]
				const CStringW objFile(gFileIdManager->GetFile(scopeDat->FileId()));
				if (!objFile.CompareNoCase(curEd->FileName()))
					doGlobalLookup = false;
			}
		}
	}

	const int initialCount = (int)m_tree.GetCount();
	if (doGlobalLookup)
	{
		g_pGlobDic->ClassBrowse(scope.c_str(), g_CVAClassView->m_tree, pNMTreeView->itemNew.hItem, FALSE);
		GetSysDic()->ClassBrowse(scope.c_str(), g_CVAClassView->m_tree, pNMTreeView->itemNew.hItem, FALSE);
	}
	int memCount = (int)m_tree.GetCount();

	if (candidateForLocal && initialCount == memCount && memCount <= 1 && mp)
	{
		// [case: 838] [case: 66202] check for local class/struct/... defined in source file
		if (!scopeDat)
		{
			scopeDat = mp->FindExact2(scope);
		}
		if (scopeDat && (scopeDat->IsMethod() || scopeDat->MaskedType() == DEFINE || scopeDat->MaskedType() == C_ENUM ||
		                 scopeDat->MaskedType() == C_ENUMITEM || scopeDat->MaskedType() == CLASS ||
		                 scopeDat->MaskedType() == STRUCT || scopeDat->MaskedType() == C_INTERFACE ||
		                 scopeDat->MaskedType() == TYPE))
		{
			mp->LocalHcbDictionary()->ClassBrowse(scope.c_str(), g_CVAClassView->m_tree, pNMTreeView->itemNew.hItem,
			                                      FALSE);
			memCount = (int)m_tree.GetCount();
		}
	}

	TVSORTCB tvs;
	tvs.hParent = hclass;
	tvs.lpfnCompare = MyCompareProc;
	tvs.lParam = (LPARAM)&m_tree;
	HTREEITEM cch = m_tree.GetChildItem(hclass);
	CStringW txt = GetTreeItemText(m_tree.GetSafeHwnd(), cch);
	m_tree.SortChildrenCB(&tvs);

	WTString lst = scope + '\f';
	WTString bcl = mp ? mp->GetBaseClassList(WTString(scope)) : NULLSTR;
	token t = bcl;
	DB_READ_LOCK;
	while (t.more())
	{
		WTString bc = t.read("\f");
		DType* data = mp ? mp->FindExact(bc) : nullptr;
		uint type = data ? data->MaskedType() : 0u;
		if (type != CLASS && type != STRUCT && type != C_INTERFACE /*&& type != TYPE*/)
			continue;

		if (bc.IsEmpty() || lst.contains(bc + '\f'))
			continue;

		lst += bc + '\f';
		WTString title = bc;
		title.ReplaceAll(":", ".");
		if (title.GetLength() && title[0] == '.')
			title = title.Mid(1);
		int img = GetTypeImgIdx(type, data ? data->Attributes() : 0);

		HTREEITEM hbaseclass = InsertTreeItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE,
		                                      DecodeScope(title).Wide(), img, img, 0, 0, NULL, hclass, TVI_LAST);
		InsertTreeItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, DecodeScope(title).Wide(), img, img, 0,
		               0, NULL, hbaseclass, TVI_LAST);
	}

	if (m_tree.GetCount() > 0 && memCount < 2)
		m_tree.Expand(m_tree.GetChildItem(hclass), TVE_EXPAND);
}

HTREEITEM CVAClassView::GetNextTreeItem(HTREEITEM hti, HTREEITEM hti_selected)
{
	HTREEITEM nxt;
	// has this item got any children
	if (m_tree.ItemHasChildren(hti))
	{
		nxt = m_tree.GetNextItem(hti, TVGN_CHILD);
		if (nxt)
			return nxt;
	}

	if (m_tree.GetNextItem(hti, TVGN_NEXT) != NULL)
	{
		// the next item at this level
		nxt = m_tree.GetNextItem(hti, TVGN_NEXT);
		if (nxt)
			return nxt;
	}

	// return the next item after our parent
	hti = m_tree.GetParentItem(hti);
	if (hti == hti_selected)
	{
		// no parent
		return NULL;
	}

	while (m_tree.GetNextItem(hti, TVGN_NEXT) == NULL)
	{
		hti = m_tree.GetParentItem(hti);
		if (hti == hti_selected || hti == NULL)
			return NULL;
	}
	// next item that follows our parent
	return m_tree.GetNextItem(hti, TVGN_NEXT);
}

void CVAClassView::ExpandAll()
{
	LogElapsedTime let("VaView::ExpandAll");

	// wait dialog variables
	int expandAllCounter = 0;
	DWORD tickCount = ::GetTickCount();
	CComPtr<IVsThreadedWaitDialog> waitDlg = nullptr;

	// preparation
	CWaitCursor curs;
	SetRedraw(FALSE);
	SetExpandAllMode(true);
	HTREEITEM hti_selected = m_tree.GetSelectedItem();
	HTREEITEM hti = hti_selected;

	// expand all
	while (hti)
	{
		if (g_loggingEnabled)
		{
			HTREEITEM debug = hti;
			int level = 0;
			while ((debug = m_tree.GetParentItem(debug)) != NULL)
				level++;
			CString debugText;
			for (int i = 0; i < level; i++)
				debugText += "\t";
			debugText += m_tree.GetItemText(hti);
			debugText += "\n";
			Log2((const char*)debugText);
		}

		if (m_tree.ItemHasChildren(hti))
		{
			m_tree.Expand(hti, TVE_EXPAND);
			expandAllCounter++;
			if (expandAllCounter % 10 == 0)
			{
				if (ManageWaitDialog(waitDlg, expandAllCounter, tickCount))
					break; // cancel button was hit
			}
		}

		if (!m_tree.ItemHasChildren(hti) && hti == hti_selected)
			break;

		hti = GetNextTreeItem(hti, hti_selected);
	}

	// finishing
	SetRedraw(TRUE);
	m_tree.Select(hti_selected, TVGN_CARET); // restore user selection
	SetExpandAllMode(false);
	m_tree.RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
	RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
	m_titlebox.GetComboBoxCtrl()->RedrawWindow();
	if (waitDlg)
		waitDlg->EndWaitDialog(nullptr);
}

void CVAClassView::ExpandItemIncludes()
{
	DB_READ_LOCK;
	HTREEITEM hBase = m_tree.GetSelectedItem();
	if (!m_tree.ItemHasChildren(hBase) || m_tree.GetChildItem(hBase) == NULL)
	{
		DType* baseFile = (DType*)m_tree.GetItemData(hBase);
		if (!baseFile)
			return;

		CStringW curFile;
		if (baseFile->MaskedType() == vaInclude)
			curFile = gFileIdManager->GetFile(baseFile->Def());
		else if (baseFile->MaskedType() == vaIncludeBy)
			curFile = gFileIdManager->GetFile(baseFile->FileId());
		else
		{
			_ASSERTE(!"huh?");
			return;
		}
		ListIncludes(hBase, curFile, false);
	}

	if (m_tree.GetChildItem(hBase) == NULL && !mExpandAllMode)
	{
		// no children; remove '+'
		RemoveExpandable(hBase);
	}
	else
	{
		TempTrue t(mIsExpanding);
		m_tree.Expand(hBase, TVE_EXPAND);
	}
}

void CVAClassView::OnHcbTvnSelectionChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	//	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	*pResult = 0;

	// #if defined(_DEBUG) && !defined(SEAN)
	// 	HTREEITEM i = m_tree.GetSelectedItem();
	// 	DType *data = (DType *)m_tree.GetItemData(i);
	// 	if(!data){
	// 		WTString sym = m_tree.GetItemText(i);
	// 		EdCntPtr curEd = g_currentEdCnt;
	// 		if(curEd)
	// 			data = curEd->m_pmparse->FindExact(sym);
	// 	}
	// 	if (data) {
	// 		if(!m_tree.GetChildItem(i))
	// 		{
	// 			MultiParse mp;
	// 			WTString bcl = mp.GetBaseClassList(data->SymScope());
	// 			if(bcl.GetLength())
	// 			{
	// 				InsertTreeItem(L"...", i);
	// 				m_tree.ValidateRect(NULL);
	// 				m_tree.SetRedraw(TRUE);
	// 			}
	// 		}
	// 	}
	// #endif // _DEBUG && !SEAN
}

void CVAClassView::GotoItemSymbol(HTREEITEM item, GoAction action /*= Go_Default*/)
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftHcb);
	try
	{
		DB_READ_LOCK;
		DType* data = (DType*)m_tree.GetItemData(item);
		if (data && data->MaskedType() == vaInclude)
		{
			// [case: 7156]
			CStringW f;
			int ln = 0;

			if (Go_Declaration == action || data->Def().IsEmpty() || -1 != data->Def().Find("(unresolved)"))
			{
				// for declaration, go to the line that the include is on.
				// for unresolved includes, go to the line of the directive.
				f = gFileIdManager->GetFile(data->FileId());
				ln = data->Line();
			}
			else
			{
				// for definition (& default), open the file
				f = data->Def().Wide();
				f = gFileIdManager->GetFile(f);
			}

			if (::IsFile(f))
				::DelayFileOpen(f, ln, NULL, TRUE);
			return;
		}

		if (data && data->MaskedType() == vaIncludeBy)
		{
			// [case: 7156]
			const CStringW f(gFileIdManager->GetFile(data->FileId()));
			if (::IsFile(f))
				::DelayFileOpen(f, data->Line(), NULL, TRUE);
			return;
		}

		if (!data || data->IsEmpty())
		{
			WTString txt = GetTreeItemText(m_tree.GetSafeHwnd(), item);
			EdCntPtr curEd = g_currentEdCnt;
			if (curEd)
			{
				MultiParsePtr mp(curEd->GetParseDb());
				data = mp->FindExact(txt);
				if (!data)
				{
					// [case: 71675] change "foo.bar" to ":foo:bar"
					txt = DB_SEP_STR + txt;
					txt.ReplaceAll(".", DB_SEP_STR);
					data = mp->FindExact(txt);
				}
			}
		}

		::GotoSymbol(action, data);
	}
	catch (...)
	{
		VALOGEXCEPTION("VCVGo:");
		GetOutline(TRUE);
	}
}

WTString CVAClassView::GetTooltipText(HTREEITEM item)
{
	WTString txt;
	if (mTheSameTreeCtrl->GetSafeHwnd() && !mTheSameTreeCtrl->AllowTooltip())
		return txt;

	try
	{
		DB_READ_LOCK;
		DType* data = (DType*)m_tree.GetItemData(item);
		if (!data)
		{
			const WTString sym(GetTreeItemText(m_tree.GetSafeHwnd(), item));
			EdCntPtr curEd = g_currentEdCnt;
			if (curEd)
			{
				MultiParsePtr mp(curEd->GetParseDb());
				data = mp->FindExact(sym);
			}
		}

		if (data)
		{
			if (data->MaskedType() == vaInclude)
			{
				// [case: 7156]
				CStringW f(data->Def().Wide());
				if (f.IsEmpty() || f == ROOT_NODE_W)
					return txt;
				f = gFileIdManager->GetFile(f);
				const WTString fa(f);
				txt.WTFormat("%s\n\nincluded at line %d", fa.c_str(), data->Line());
			}
			else if (data->MaskedType() == vaIncludeBy)
			{
				// [case: 7156]
				CStringW f(data->Def().Wide());
				if (f.IsEmpty() || f == ROOT_NODE_W)
					return txt;
				f = gFileIdManager->GetFile(f);
				const WTString fa(::Basename(f));
				f = gFileIdManager->GetFile(data->FileId());
				const WTString f1(f);
				txt.WTFormat("%s\n\nincludes %s at line %d", f1.c_str(), fa.c_str(), data->Line());
			}
			else
			{
				token t = CleanDefForDisplay(data->Def(), gTypingDevLang);
				WTString def = t.read("\f");
				const WTString sym(data->SymScope());
				WTString cmnt = GetCommentForSym(sym);
				const WTString location((LPCSTR)CString(GetSymbolLocation(*data)));
				if (!location.IsEmpty())
					cmnt += "\nFile: " + location;
				if (cmnt.GetLength())
					cmnt.prepend("\n");
				txt = DecodeScope(def) + cmnt;
			}
		}
	}
	catch (...)
	{
		m_lastSym.Empty();
		VALOGEXCEPTION("VACV:");
		BOOL lock = m_lock;
		m_lock = FALSE;
		if (ClassViewSym.GetLength() && ClassViewSym != DB_SEP_CHR)
			::RefreshHcb();
		else
			GetOutline(TRUE);
		m_lock = lock;
	}

	return txt;
}

DWORD
CVAClassView::QueryStatus(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_RefactorExtractMethod:
	case icmdVaCmd_RefactorPromoteLambda:
	case icmdVaCmd_RefactorMoveImplementation:
	case icmdVaCmd_RefactorDocumentMethod:
	case icmdVaCmd_RefactorCreateImplementation:
	case icmdVaCmd_RefactorCreateDeclaration:
	case icmdVaCmd_RefactorChangeVisibility:
	case icmdVaCmd_RefactorPopupMenu:
	case icmdVaCmd_RefactorAddMember:
	case icmdVaCmd_RefactorAddSimilarMember:
	case icmdVaCmd_RefactorExpandMacro:
	case icmdVaCmd_RefactorRenameFiles:
	case icmdVaCmd_RefactorCreateFile:
	case icmdVaCmd_RefactorMoveSelToNewFile:
	case icmdVaCmd_RefactorMoveImplementationToHdr:
	case icmdVaCmd_RefactorConvertBetweenPointerAndInstance:
	case icmdVaCmd_RefactorSimplifyInstanceDeclaration:
	case icmdVaCmd_RefactorAddForwardDeclaration:
	case icmdVaCmd_RefactorConvertEnum:
	case icmdVaCmd_RefactorMoveClassToNewFile:
	case icmdVaCmd_RefactorSortClassMethods:
		return 0; // no ClassView support
	case icmdVaCmd_RefactorEncapsulateField:
	case icmdVaCmd_RefactorChangeSignature:
	case icmdVaCmd_RefactorRename:
	case icmdVaCmd_FindReferences: {
		HTREEITEM selItem = m_tree.GetSelectedItem();
		if (selItem)
		{
			DB_READ_LOCK;
			DType* data = (DType*)m_tree.GetItemData(selItem);
			if (data)
			{
				try
				{
					if (icmdVaCmd_RefactorEncapsulateField == cmdId)
						return VARefactorCls::CanEncapsulateField(data) ? 1u : 0u;
					if (icmdVaCmd_RefactorChangeSignature == cmdId)
						return VARefactorCls::CanChangeSignature(data) ? 1u : 0u;
					if (icmdVaCmd_RefactorRename == cmdId)
						return VARefactorCls::CanRename(data) ? 1u : 0u;
					if (icmdVaCmd_FindReferences == cmdId)
						return VARefactorCls::CanFindReferences(data) ? 1u : 0u;
				}
				catch (...)
				{
					ClearHcb();
				}
			}
		}
	}
		return 0u;
	}

	return (DWORD)-2;
}

HRESULT
CVAClassView::Exec(DWORD cmdId)
{
	switch (cmdId)
	{
	case icmdVaCmd_RefactorEncapsulateField:
		ExecContextMenuCommand(VARef_EncapsulateField, GetSelItemClassData());
		break;
	case icmdVaCmd_RefactorChangeSignature:
		ExecContextMenuCommand(VARef_ChangeSignature, GetSelItemClassData());
		break;
	case icmdVaCmd_FindReferences:
		ExecContextMenuCommand(VARef_FindUsage, GetSelItemClassData());
		break;
	case icmdVaCmd_RefactorRename:
		ExecContextMenuCommand(VARef_Rename, GetSelItemClassData());
		break;
	default:
		return OLECMDERR_E_NOTSUPPORTED;
	}

	return S_OK;
}

#define GOTO_DEFINITION (VARef_Count + 1)
#define GOTO_DECLARATION (VARef_Count + 2)
#define ID_CMD_OPEN_FILE (VARef_Count + 3)
#define ID_CMD_GOTO_DIRECTIVE (VARef_Count + 4)
#define ID_CMD_EXPAND_ALL_AND_COPY (VARef_Count + 5)
#define ID_CMD_COPY (VARef_Count + 6)
#define ID_CMD_COPY_DEC (VARef_Count + 7)

void CVAClassView::DisplayContextMenu(CPoint pos, DType* sym)
{
	RefactoringActive active;
	DB_READ_LOCK;
	try
	{
		PopupMenuXP xpmenu;

		if (sym)
		{
			// xpmenu.AddMenuItem(ID_CMD_COPY, MF_BYPOSITION, "Cop&y");
			if (sym->MaskedType() == vaInclude)
			{
				if (sym->Def() != ROOT_NODE)
				{
					xpmenu.AddMenuItem(ID_CMD_OPEN_FILE, MF_BYPOSITION, "&Open file");
					xpmenu.AddMenuItem(ID_CMD_GOTO_DIRECTIVE, MF_BYPOSITION, "&Goto include directive");
				}
			}
			else if (sym->MaskedType() == vaIncludeBy)
			{
				if (sym->Def() != ROOT_NODE)
					xpmenu.AddMenuItem(ID_CMD_OPEN_FILE, MF_BYPOSITION, "&Goto include directive");
			}
			else if (sym->IsMethod() && IsCFile(gTypingDevLang))
			{
				xpmenu.AddMenuItem(GOTO_DEFINITION, MF_BYPOSITION, "&Goto Definition", ICONIDX_REFERENCE_GOTO_DEF);
				xpmenu.AddMenuItem(GOTO_DECLARATION, MF_BYPOSITION, "Goto &Declaration", ICONIDX_REFERENCE_GOTO_DECL);
				xpmenu.AddMenuItem(ID_CMD_COPY_DEC, MF_BYPOSITION, "&Copy Declaration", ID_CMD_COPY_DEC);
			}
			else
			{
				xpmenu.AddMenuItem(GOTO_DECLARATION, MF_BYPOSITION, "&Goto", ICONIDX_REFERENCE_GOTO_DEF);
				xpmenu.AddMenuItem(ID_CMD_COPY_DEC, MF_BYPOSITION, "&Copy Declaration", ID_CMD_COPY_DEC);
			}
			xpmenu.AddMenuItem(ID_CMD_EXPAND_ALL_AND_COPY, MF_BYPOSITION, "&Expand Descendants and Copy");
			xpmenu.AddMenuItem(0, MF_SEPARATOR, "");
		}

		xpmenu.AddMenuItem(IDC_UpdateHcbOnHover, MF_BYPOSITION | (Psettings->mUpdateHcbOnHover ? MF_CHECKED : 0u),
		                   "Update on mouse &hover", IDC_UpdateHcbOnHover);
		xpmenu.AddMenuItem(IDC_UPDATEWITHCURRENTSCOPE,
		                   MF_BYPOSITION | ((Psettings->m_nHCBOptions & 0x1) ? MF_CHECKED : 0u),
		                   "&Update with current scope", IDC_UPDATEWITHCURRENTSCOPE);
		xpmenu.AddMenuItem(IDC_UPDATEWITHCARET, MF_BYPOSITION | ((Psettings->m_nHCBOptions & 0x2) ? MF_CHECKED : 0u),
		                   "U&pdate on position change", IDC_UPDATEWITHCARET);
		if (IsFileWithIncludes(gTypingDevLang))
			xpmenu.AddMenuItem(IDC_AutoListIncludes, MF_BYPOSITION | (Psettings->mAutoListIncludes ? MF_CHECKED : 0u),
			                   "Display includes &when at top of file", IDC_AutoListIncludes);

		if (sym && sym->MaskedType() != vaInclude && sym->MaskedType() != vaIncludeBy)
		{
			BOOL doSep = TRUE;

			if (VARefactorCls::CanChangeSignature(sym))
			{
				xpmenu.AddMenuItem(0, MF_SEPARATOR, "");
				doSep = FALSE;
				// use a different shortcut key than normal to avoid clash on &Goto
				xpmenu.AddMenuItem(VARef_ChangeSignature, MF_BYPOSITION, "Change S&ignature",
				                   ICONIDX_REFACTOR_CHANGE_SIGNATURE);
			}

			if (VARefactorCls::CanEncapsulateField(sym))
			{
				if (doSep)
				{
					xpmenu.AddMenuItem(0, MF_SEPARATOR, "");
					doSep = FALSE;
				}
				xpmenu.AddMenuItem(VARef_EncapsulateField, MF_BYPOSITION, "Encapsulate &Field",
				                   ICONIDX_REFACTOR_ENCAPSULATE_FIELD);
			}

			if (VARefactorCls::CanRename(sym))
			{
				if (doSep)
				{
					xpmenu.AddMenuItem(0, MF_SEPARATOR, "");
					doSep = FALSE;
				}
				xpmenu.AddMenuItem(VARef_Rename, MF_BYPOSITION, "Re&name", ICONIDX_REFACTOR_RENAME);
			}

			if (VARefactorCls::CanFindReferences(sym))
			{
				xpmenu.AddMenuItem(0, MF_SEPARATOR, "");
				xpmenu.AddMenuItem(VARef_FindUsage, MF_BYPOSITION, "Find &References", ICONIDX_REFERENCE_FIND_REF);
			}
		}

		const UINT flag = (UINT)xpmenu.TrackPopupMenuXP(this, pos.x, pos.y);
		if (flag)
			ExecContextMenuCommand(flag, sym);
	}
	catch (...)
	{
		VALOGEXCEPTION("VACV::ContextMenu:");
	}
}

void CVAClassView::ExecContextMenuCommand(UINT menuCmd, DType* symData)
{
	RefactoringActive active;
	DB_READ_LOCK;
	try
	{
		switch (menuCmd)
		{
		case VARef_FindUsage:
			if (gVaService && symData)
				gVaService->FindReferences(
				    FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments | FREF_FLG_FindAutoVars,
				    GetTypeImgIdx(symData->MaskedType(), symData->Attributes()), symData->SymScope());
			break;
		case IDC_UPDATEWITHCURRENTSCOPE:
			Psettings->m_nHCBOptions ^= 1;
			break;
		case IDC_UPDATEWITHCARET:
			Psettings->m_nHCBOptions ^= 2;
			break;
		case IDC_AutoListIncludes:
			Psettings->mAutoListIncludes ^= 1;
			break;
		case IDC_UpdateHcbOnHover:
			Psettings->mUpdateHcbOnHover ^= 1;
			break;
		case VARef_ChangeSignature:
			if (symData)
				VARefactorCls::ChangeSignature(symData);
			break;
		case ID_CMD_GOTO_DIRECTIVE:
			_ASSERTE(symData && (symData->MaskedType() == vaInclude || symData->MaskedType() == vaIncludeBy));
			GotoItemSymbol(m_tree.GetSelectedItem(), Go_Declaration);
			break;
		case ID_CMD_OPEN_FILE:
			_ASSERTE(symData && (symData->MaskedType() == vaInclude || symData->MaskedType() == vaIncludeBy));
			GotoItemSymbol(m_tree.GetSelectedItem(), Go_Definition);
			break;
		case GOTO_DEFINITION:
			if (!symData)
				GotoItemSymbol(m_tree.GetSelectedItem(), Go_Definition);
			else if (symData)
				::GotoSymbol(Go_Definition, symData);
			break;
		case GOTO_DECLARATION:
			if (!symData)
				GotoItemSymbol(m_tree.GetSelectedItem(), Go_Declaration);
			else if (symData)
				::GotoSymbol(Go_Declaration, symData);
			break;
		case ID_CMD_COPY_DEC:
			if (symData)
			{
				WTString txt = DecodeScope(symData->Def());
				SetClipText(txt);
			}
			break;
		case VARef_Rename:
			if (symData)
			{
				RenameReferencesDlg ren(symData->SymScope());
				ren.DoModal();
			}
			break;
		case VARef_EncapsulateField:
			if (symData)
			{
				VARefactorCls ref;
				ref.Encapsulate(symData);
			}
			break;
		case ID_CMD_EXPAND_ALL_AND_COPY:
			ExpandAll();
			CopyAll();
			break;
		case ID_CMD_COPY:
			CopyAll();
			break;
		default:
			_ASSERTE(!"unhandled hcb context menu command");
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VACV::ExecCmd:");
	}
}

void CVAClassView::HierarchicalBrowse(WTString& scope, DType* data, DType* dataScope, WTString& sym)
{
	if (gTestsActive)
		mInfoForLogging.WTFormat("HierarchicalBrowse %s %s %s", ClassViewSym.c_str(), scope.c_str(), sym.c_str());

	mIncludeEdLine = -1;
	mIncludeCurFile.Empty();
	mRootIncludeList.clear();
	mRootIncludeByList.clear();
	m_tree.SetRedraw(FALSE);
	mTheSameTreeCtrl->ClearAll();
	m_tree.ModifyStyle(TVS_LINESATROOT, 0);
	scope.ReplaceAll(":", ".");

	int img = (data && dataScope) ? GetTypeImgIdx(dataScope->MaskedType(), dataScope->Attributes()) : ICONIDX_BLANK;

	// Don't do this since child items fail to be found if the parent isn't properly named
	// Clean up scope before displaying it
	// change "if-101" to just "if"
	// 	const WTString origScope(scope);
	// 	if(origScope.Find("-") != -1)
	// 	{
	// 		CString cleanedScope;
	// 		for(LPCSTR p = origScope;*p;p++)
	// 		{
	// 			if(*p == '-') // change "if-101" to just "if"
	// 				while(wt_isdigit(*(++p)));
	// 			if(*p)
	// 				cleanedScope += *p;
	// 			else
	// 				break;
	// 		}
	// 		scope = cleanedScope;
	// 	}

	SetTitle(&scope.c_str()[1], img);
	HTREEITEM hclass = InsertTreeItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE,
	                                  DecodeScope(&scope.c_str()[1]).Wide(), img, img, 0, 0, NULL, TVI_ROOT, TVI_LAST);
	InsertTreeItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, DecodeScope(&scope.c_str()[1]).Wide(), img,
	               img, 0, 0, NULL, hclass, TVI_LAST);
	m_tree.Expand(hclass, TVE_EXPAND);
	m_tree.SetRedraw(TRUE);
	m_tree.Invalidate(FALSE);

	// select sym
	HTREEITEM sel = NULL;
	if (!sym.GetLength())
	{
		TVSORTCB tvs;
		tvs.hParent = hclass;
		tvs.lpfnCompare = MyCompareProc;
		tvs.lParam = (LPARAM)&m_tree;
		if (hclass != TVI_ROOT)
			sel = hclass;
	}
	else
	{
		// special case if sym is an operator.
		// if sym is an operator, then the sym is =, ==, etc.
		// compare against tree item will fail since tree text includes
		// the word operator (and sym doesn't).
		bool isOperator = false;
		for (int idx = 0; idx < sym.GetLength() && idx < 3; ++idx)
		{
			if (ISCSYM(sym[idx]))
				break;
			if ((0 == idx && sym.GetLength() == 1) || 1 == idx)
			{
				isOperator = true;
				break;
			}
		}

		HTREEITEM i = m_tree.GetChildItem(hclass);
		const int kFirstPos = isOperator ? 8 : 0;
		const int kSecondPos = isOperator ? 9 : 1;
		while (i)
		{
			const WTString txt(GetTreeItemText(m_tree.GetSafeHwnd(), i));
			const int n = txt.Find(sym);
			if (n >= kFirstPos && n <= kSecondPos &&
			    (txt.GetLength() == (n + sym.GetLength()) ||
			     !ISCSYM(txt[n + sym.GetLength()]))) // make sure sym matches length
			{
				if (isOperator)
				{
					const DType* curData = (DType*)m_tree.GetItemData(i);
					if (curData->SymHash() == data->SymHash())
						sel = i;
				}
				else
					sel = i;

				if (sel)
					break;
			}
			i = m_tree.GetNextItem(i, TVGN_NEXT);
		}
	}

	if (sel)
	{
		int img2, imgSel;
		m_tree.GetItemImage(sel, img2, imgSel);
		if (m_fileView)
			img2 = 27; // ?!
		m_tree.SelectItem(sel);
	}
}

void CVAClassView::FlatBrowse()
{
	if (gTestsActive)
		mInfoForLogging.WTFormat("FlatBrowse %s", ClassViewSym.c_str());

	// clear list
	mIncludeEdLine = -1;
	mIncludeCurFile.Empty();
	mRootIncludeList.clear();
	mRootIncludeByList.clear();
	m_tree.SetRedraw(FALSE);
	SetTitle("");
	mTheSameTreeCtrl->ClearAll();
	m_tree.ModifyStyle(TVS_LINESATROOT, 0);
	BrowseSymbol(ClassViewSym, m_tree.m_hWnd, m_tree.GetRootItem());
	if (m_tree.GetCount() > 1)
	{
		m_tree.SortChildren(TVI_ROOT);

		// remove duplicates
		WTString ltxt;
		HTREEITEM hNext;
		hNext = m_tree.GetRootItem();
		while (hNext)
		{
			WTString txt = GetTreeItemText(m_tree.GetSafeHwnd(), hNext);
			HTREEITEM hTmp = hNext;
			hNext = m_tree.GetNextItem(hNext, TVGN_NEXT);
			if (txt == ltxt)
			{
				m_tree.DeleteItem(hTmp);
			}
			ltxt = txt;
		}

		HTREEITEM hItem = m_tree.GetSelectedItem();
		m_tree.SelectItem(hItem);
		for (int i = 4; i && m_tree.GetNextItem(hItem, TVGN_NEXT); i--)
		{
			hItem = m_tree.GetNextItem(hItem, TVGN_NEXT);
		}

		m_tree.SetRedraw(TRUE);
		m_tree.EnsureVisible(hItem);

		// 		SCROLLINFO si;
		// 		si.cbSize = sizeof(SCROLLINFO);
		// 		si.fMask = SIF_POS;
		// 		si.nPos = 0;
		// 		if (m_tree.GetScrollInfo(SB_HORZ, &si) && si.nPos)
		// 		{
		// 			// sometimes tree is scrolled right and icons aren't visible
		// 			m_tree.SendMessage(WM_HSCROLL, SB_THUMBPOSITION, 0);
		//
		// 			// these 2 calls are a hack to fix strange focus problem that
		// 			// happens due to the WM_HSCROLL message - but the fix for the
		// 			// focus problem is worse than the problem the WM_HSCROLL
		// 			// message addresses.  see HCB for _wremove, IsFile, CPPUNIT_ASSERT
		// 			::SendMessage(MainWndH, VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR)"VAssistX.VAView", 0);
		// 			::SendMessage(MainWndH, VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR)"Window.ActivateDocumentWindow", 0);
		// 		}
	}
	else
	{
		SetTitle("");
		mTheSameTreeCtrl->ClearAll();
	}

	m_tree.SetRedraw(TRUE);
	m_tree.Invalidate(FALSE);
	m_tree.UpdateWindow();
}

inline bool SortIncludesLT(DType& c1, DType& c2)
{
	CStringW f1(gFileIdManager->GetFile(c1.Def()));
	CStringW f2(gFileIdManager->GetFile(c2.Def()));
	int ftype1 = GetFileType(f1);
	int ftype2 = GetFileType(f2);
	if (ftype1 != ftype2)
	{
		// sort headers above other files
		if (ftype1 == Header)
			return true;

		if (ftype2 == Header)
			return false;
	}

	// otherwise, sort basename in alpha order
	f1 = Basename(f1);
	f2 = Basename(f2);

	int cmp = f1.CompareNoCase(f2);
	if (0 == cmp)
	{
		if (c1.Line() < c2.Line())
			return true;
	}
	else if (0 < cmp)
		return true;

	return false;
}

inline bool SortIncludeBysHeaderFirstLT(const DType& c1, const DType& c2)
{
	CStringW f1(gFileIdManager->GetFile(c1.FileId()));
	CStringW f2(gFileIdManager->GetFile(c2.FileId()));
	int ftype1 = GetFileType(f1);
	int ftype2 = GetFileType(f2);
	if (ftype1 != ftype2)
	{
		// sort headers above other files
		if (ftype1 == Header)
			return true;

		if (ftype2 == Header)
			return false;
	}

	// otherwise, sort basename in alpha order
	f1 = Basename(f1);
	f2 = Basename(f2);

	int cmp = f1.CompareNoCase(f2);
	if (0 == cmp)
	{
		if (c1.Line() < c2.Line())
			return true;
	}
	else if (cmp < 0)
		return true;

	return false;
}

bool CVAClassView::IsCircular(HTREEITEM hti, const CStringW& path, const CStringW& childItemPath)
{
	if (childItemPath == path)
		return true;

	while (hti && hti != TVI_ROOT)
	{
		DType* dtype = (DType*)m_tree.GetItemData(hti);
		if (dtype)
		{
			CStringW path2 = dtype->FilePath();
			if (path2 == childItemPath)
				return true;
		}
		hti = m_tree.GetParentItem(hti);
	}

	return false;
}

bool CVAClassView::ListIncludes(HTREEITEM hParentItem, const CStringW& curFile, bool didLock)
{
	const bool kInitialLoad = TVI_ROOT == hParentItem;
	// no change if locked, unless expanding a child item
	if (m_lock && kInitialLoad)
		return true;

	LogElapsedTime et("HCB::ListIncludes");
	mTheSameTreeCtrl->SetColorable(false);
	::mySetProp(m_tree.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	CWaitCursor curs;
	EdCntPtr curEd = g_currentEdCnt;
	const int kEdLine = kInitialLoad && curEd ? curEd->CurLine() : -1;
	DType* baseDt = nullptr;
	if (!kInitialLoad)
	{
		baseDt = (DType*)m_tree.GetItemData(hParentItem);
		CStringW label = GetTreeItemText(m_tree.GetSafeHwnd(), hParentItem);
		if (label.Find(L"(c") != -1) // circular reference. they aren't expanded further
			return false;
	}
	const bool kDoIncludes = kInitialLoad || (baseDt && baseDt->MaskedType() == vaInclude);
	const bool kDoIncludeBys = kInitialLoad || (baseDt && baseDt->MaskedType() == vaIncludeBy);

	if (gTestsActive)
		mInfoForLogging = "listIncludes";

	DTypeList incList, incByList;
	if (kDoIncludes)
	{
		// get list of includes for cur file id
		IncludesDb::GetIncludes(curFile, DTypeDbScope::dbSystemIfNoSln, incList);
		incList.FilterDupesAndGotoDefs();
		incList.sort(SortIncludesLT);
	}

	if (kDoIncludeBys)
	{
		IncludesDb::GetIncludedBys(curFile, DTypeDbScope::dbSlnAndSys, incByList);
		incByList.sort(SortIncludeBysHeaderFirstLT);
	}

	if (kInitialLoad)
	{
		// flicker reduction
		if (curFile == mIncludeCurFile && mRootIncludeList == incList && mRootIncludeByList == incByList)
		{
			if (kEdLine != mIncludeEdLine && mRootIncludeList.size() && m_tree.GetCount())
			{
				// change selected item
				HTREEITEM it = m_tree.GetChildItem(TVI_ROOT);
				HTREEITEM selIt = NULL;
				for (it = m_tree.GetChildItem(it); it; it = m_tree.GetNextSiblingItem(it))
				{
					DType* dt = (DType*)m_tree.GetItemData(it);
					_ASSERTE(dt);
					if (dt && kEdLine == dt->Line())
					{
						_ASSERTE(dt->MaskedType() == vaInclude);
						selIt = it;
						break;
					}
				}

				m_tree.SelectItem(selIt);
				mIncludeEdLine = kEdLine;
			}

			return false;
		}
	}

	// clear list
	if (!didLock)
		m_tree.SetRedraw(FALSE);

	if (kInitialLoad)
		mTheSameTreeCtrl->ClearAll();

	bool added = false;
	enum eNodeType
	{
		NotSpecial,
		Cicular,
		External,
	};
	if (kDoIncludes)
	{
		// get list of includes for cur file id
		TVINSERTSTRUCTW tvis;
		tvis.hInsertAfter = TVI_LAST;
		tvis.item.state = 0;
		tvis.item.stateMask = 0;
		tvis.item.cChildren = 0;
		tvis.hParent = hParentItem;
		if (kInitialLoad && incList.size())
		{
			// need to create a root 'includes' node - only for the base file
			tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_PARAM;
			tvis.item.iImage = tvis.item.iSelectedImage = ICONIDX_BLANK;
			tvis.item.pszText = (LPWSTR)(LPCWSTR)L"includes";
			DType blank("+ic:", ROOT_NODE, vaInclude, 0, 0);
			tvis.item.lParam = (LPARAM)&blank;
			tvis.hParent = InsertTreeItem(&tvis);
		}
		tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_PARAM | TVIF_CHILDREN;
		tvis.item.cChildren = I_CHILDRENCALLBACK;

		for (DTypeList::reverse_iterator it = incList.rbegin(); it != incList.rend(); ++it)
		{
			DType& dt = *it;
			CStringW curInc(dt.Def().Wide());
			eNodeType nodeType = NotSpecial;
			if (curInc.IsEmpty())
			{
				curInc = L"(unresolved)";
				tvis.item.iImage = tvis.item.iSelectedImage = ICONIDX_FILE_H;
			}
			else if (-1 != curInc.Find(L"(unresolved)"))
			{
				tvis.item.iImage = tvis.item.iSelectedImage = ICONIDX_FILE_H;
			}
			else
			{
				curInc = gFileIdManager->GetFile(curInc);

				EdCntPtr curEd2 = g_currentEdCnt;
				if (curEd2)
				{
					MultiParsePtr mp(curEd2->GetParseDb());
					DTypePtr incData{mp->GetFileData(curInc)};
					if (incData && (incData->IsSystemSymbol() || incData->IsDbExternalOther())) // is external
					{
						if (mExpandAllMode)
							continue;
						else
							nodeType = External;
					}
					else
					{ // is circular
						if (IsCircular(hParentItem, curFile, curInc))
							nodeType = Cicular;
					}
				}

				const int fType = ::GetFileTypeByExtension(curInc);
				if (Binary == fType)
					continue;

				tvis.item.iImage = tvis.item.iSelectedImage = ::GetFileImgIdx(curInc);
				curInc = ::Basename(curInc);
			}

			switch (nodeType)
			{
			case Cicular:
				curInc += L" (circular)";
				break;
			case External:
				curInc += L" (external)";
				break;
			default:
				break;
			}

			tvis.item.pszText = (LPWSTR)(LPCWSTR)curInc;
			tvis.item.lParam = (LPARAM)&dt;
			HTREEITEM item = InsertTreeItem(&tvis);
			if (item)
			{
				added = true;
				if (kEdLine == dt.Line())
					m_tree.SelectItem(item);
				if (nodeType == Cicular)
				{
					// no children; remove '+'
					RemoveExpandable(item);
				}
			}
		}

		if (added)
		{
			TempTrue t(mIsExpanding);
			m_tree.Expand(tvis.hParent, TVE_EXPAND);
		}
		else if (kInitialLoad && tvis.hParent != TVI_ROOT && incList.size())
		{
			m_tree.DeleteItem(tvis.hParent);
		}
	}

	if (kDoIncludeBys && incByList.size())
	{
		TVINSERTSTRUCTW tvis;
		tvis.hParent = hParentItem;
		tvis.hInsertAfter = TVI_LAST;
		tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_PARAM;
		tvis.item.state = 0;
		tvis.item.stateMask = 0;

		if (kInitialLoad)
		{
			// create root node for include bys to hang from
			tvis.item.iImage = tvis.item.iSelectedImage = ICONIDX_BLANK;
			tvis.item.pszText = (LPWSTR)(LPCWSTR)L"included by";
			DType blank("+ib:_", ROOT_NODE, vaIncludeBy, 0, 0);
			tvis.item.lParam = (LPARAM)&blank;
			tvis.hParent = InsertTreeItem(&tvis);
		}

		tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_PARAM | TVIF_CHILDREN;
		tvis.item.cChildren = I_CHILDRENCALLBACK;

		for (DTypeList::iterator it = incByList.begin(); it != incByList.end(); ++it)
		{
			DType& dt = *it;
			CStringW curInc(gFileIdManager->GetFile(dt.FileId()));
			eNodeType nodeType = NotSpecial;
			if (IsCircular(hParentItem, curFile, curInc))
				nodeType = Cicular;

			tvis.item.iImage = tvis.item.iSelectedImage = ::GetFileImgIdx(curInc);
			curInc = ::Basename(curInc);

			if (nodeType == Cicular)
				curInc += L" (circular)";
			tvis.item.pszText = (LPWSTR)(LPCWSTR)curInc;
			tvis.item.lParam = (LPARAM)&dt;
			HTREEITEM item = InsertTreeItem(&tvis);
			if (item)
			{
				added = true;
				if (nodeType == Cicular)
				{
					// no children; remove '+'
					RemoveExpandable(item);
				}
			}
		}

		TempTrue t(mIsExpanding);
		m_tree.Expand(tvis.hParent, TVE_EXPAND);
	}

	if (kInitialLoad)
	{
		if (added)
		{
			SetTitle(WTString(::Basename(curFile)).c_str(), -1);
			mFileIdOfDependentSym = gFileIdManager->GetFileId(curFile);
		}
		else
			SetTitle("");
	}

	if (kInitialLoad)
	{
		mIncludeEdLine = kEdLine;
		mIncludeCurFile = curFile;
		mRootIncludeList.swap(incList);
		mRootIncludeByList.swap(incByList);
	}
	else
	{
		mRootIncludeList.clear();
		mRootIncludeByList.clear();
	}

	if (!didLock)
	{
		m_tree.SetRedraw(TRUE);
		m_tree.Invalidate(FALSE);
		m_tree.UpdateWindow();
	}

	return true;
}

void CVAClassView::OnDestroy()
{
	if (mTheSameTreeCtrl)
	{
		delete mTheSameTreeCtrl;
		mTheSameTreeCtrl = NULL;
	}

	__super::OnDestroy();
}

BOOL CVAClassView::OnEraseBkgnd(CDC* dc)
{
	if (CVS2010Colours::IsVS2010VAViewColouringActive() && gPkgServiceProvider)
		return 1;
	else
		return __super::OnEraseBkgnd(dc);
}

void CVAClassView::ThemeUpdated()
{
	gImgListMgr->SetImgListForDPI(m_tree, ImageListManager::bgTree, TVSIL_NORMAL);
	m_titlebox.SettingsChanged();
	Invalidate(TRUE);
}

void CVAClassView::CheckHcbForDependentData()
{
	if (!mFileIdOfDependentSym || m_lock)
		return;

	EdCntPtr curEd = g_currentEdCnt;
	if (!curEd)
	{
		ClearHcb();
		return;
	}

	const uint curFileId = gFileIdManager->GetFileId(curEd->FileName());
	if (curFileId != mFileIdOfDependentSym)
		ClearHcb();
}

void CVAClassView::FocusHcb()
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftHcb);
	if (m_tree.GetCount())
	{
		HTREEITEM selItem = m_tree.GetSelectedItem();
		if (!selItem)
		{
			selItem = m_tree.GetChildItem(TVI_ROOT);
			if (selItem)
				m_tree.SelectItem(selItem);
		}
	}

	m_tree.SetFocus();
	m_titlebox.RedrawWindow();
}

void CVAClassView::FocusHcbLock()
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftHcbLock);
	CButton* btn = (CButton*)GetDlgItem(IDC_BUTTON1);
	if (btn->GetSafeHwnd())
		btn->SetFocus();
}

CStringW CVAClassView::CopyHierarchy(HTREEITEM item, CStringW prefix)
{
	CStringW txt(prefix + GetTreeItemText(m_tree.GetSafeHwnd(), item) + L"\r\n");
	if (m_tree.ItemHasChildren(item))
	{
		item = m_tree.GetNextItem(item, TVGN_CHILD);
		while (item)
		{
			txt += CopyHierarchy(item, prefix + CStringW(L"\t"));
			item = m_tree.GetNextSiblingItem(item);
		}
	}

	return txt;
}

void CVAClassView::CopyAll()
{
	LogElapsedTime let("VaView::CopyAll");
	HTREEITEM selItem = m_tree.GetSelectedItem();
	if (!selItem)
		return;

	CWaitCursor curs;
	const CStringW txt(CopyHierarchy(selItem, CStringW()));
	::SaveToClipboard(m_hWnd, txt);

	if (gTestLogger)
	{
		gTestLogger->LogStr("HCB Copy All start:");
		gTestLogger->LogStr(WTString(txt));
		gTestLogger->LogStr("HCB Copy All end.");
	}
}

bool CVAClassView::ManageWaitDialog(CComPtr<IVsThreadedWaitDialog>& waitDlg, int& expandAllCounter, DWORD& tickCount)
{
	if (waitDlg)
	{ // update and watch out for cancel
		BOOL cancelled;
		CStringW text;
		CString__FormatW(text, L"Building tree hierarchy: %d tree nodes", expandAllCounter);
		waitDlg->GiveTimeSlice(CComBSTR(text), nullptr, 0, &cancelled);
		if (cancelled)
		{
			// waitDlg = nullptr;
			return true;
		}
	}

	if (waitDlg == nullptr && ::GetTickCount() - tickCount > 5 * 1000)
	{ // after 5 sec, we show the wait dialog
		waitDlg = ::GetVsThreadedWaitDialog();
		if (waitDlg)
		{
			CComVariant jnk;
			variant_t val;
			val.vt = VT_NULL;
			waitDlg->StartWaitDialog(CComBSTR(CStringW(IDS_APPNAME)), CComBSTR(L"Building include hierarchy..."),
			                         nullptr, 1, val, nullptr);
		}
	}

	return false;
}

bool CVAClassView::IsFileWithIncludes(int lang)
{
#ifdef IDL_LIST_INCLUDES
	return IsCFile(lang) || lang == Idl;
#else
	return IsCFile(lang);
#endif
}

void CVAClassView::RemoveExpandable(HTREEITEM item)
{
	TV_ITEM tvItem;
	ZeroMemory(&tvItem, sizeof(tvItem));
	tvItem.hItem = item;
	tvItem.mask = TVIF_CHILDREN;
	m_tree.SetItem(&tvItem);
}

HTREEITEM CVAClassView::InsertTreeItem(LPTVINSERTSTRUCTW lpInsertStruct)
{
	ASSERT(::IsWindow(m_tree.m_hWnd));
	return (HTREEITEM)::SendMessageW(m_tree.m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)lpInsertStruct);
}

HTREEITEM CVAClassView::InsertTreeItem(UINT nMask, LPCWSTR lpszItem, int nImage, int nSelectedImage, UINT nState,
                                       UINT nStateMask, LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	ASSERT(::IsWindow(m_tree.m_hWnd));
	TVINSERTSTRUCTW tvis;
	tvis.hParent = hParent;
	tvis.hInsertAfter = hInsertAfter;
	tvis.item.mask = nMask;
	tvis.item.hItem = nullptr;
	tvis.item.state = nState;
	tvis.item.stateMask = nStateMask;
	tvis.item.pszText = (LPWSTR)lpszItem;
	tvis.item.cchTextMax = 0;
	tvis.item.iImage = nImage;
	tvis.item.iSelectedImage = nSelectedImage;
	tvis.item.cChildren = 0;
	tvis.item.lParam = lParam;
	return (HTREEITEM)::SendMessageW(m_tree.m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

HTREEITEM CVAClassView::InsertTreeItem(LPCWSTR lpszItem, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	ASSERT(::IsWindow(m_hWnd));
	return InsertTreeItem(TVIF_TEXT, lpszItem, 0, 0, 0, 0, 0, hParent, hInsertAfter);
}

CStringW CVAClassView::GetTreeItemText(HWND hWnd, HTREEITEM hItem)
{
	ASSERT(::IsWindow(hWnd));
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
		::SendMessageW(hWnd, TVM_GETITEMW, 0, (LPARAM)&item);
		nRes = item.pszText ? ::wcslen_i(item.pszText) : 0;
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}
