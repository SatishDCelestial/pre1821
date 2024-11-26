// ToolTipEditView.cpp : implementation file
//

#include "stdafx.h"
#include "ToolTipEditView.h"
#include "..\expansion.h"
#include "..\fontsettings.h"
#include "..\VACompletionBox.h"
#include "../ToolTipEditCombo.h"
#include "../Settings.h"
#include "../WindowUtils.h"
#include "../PROJECT.H"
#include "../KeyBindings.h"
#include "../ImageListManager.h"
#include "../IdeSettings.h"
#include "MiniHelpFrm.h"
#include "../DpiCookbook/VsUIDpiHelper.h"
#include "../MenuXP/Draw.h"
#include "VAThemeUtils.h"
#include "VaService.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define BUTTONWIDTH (VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(16))

class SpinnerOld : public CSpinButtonCtrl
{
  public:
	SpinnerOld()
	{
	}

	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		LRESULT retval = CWnd::WindowProc(message, wParam, lParam);
		if (message == WM_DESTROY)
			PostMessage(WM_CLOSE);
		return retval;
	}
};

class SpinnerV11 : public CWnd
{
  public:
	SpinnerV11()
	{
	}

  protected:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs)
	{
		cs.lpszClass = ::GetDefaultVaWndCls();
		return __super::PreCreateWindow(cs);
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_ERASEBKGND)
			return 1;

		if (message == WM_MOUSEMOVE)
		{
			if (!mMouseCaptured)
				InvalidateRect(NULL, false);
		}

		if (message == WM_LBUTTONUP)
			CancelMouseCapture();

		if (message == WM_LBUTTONDOWN)
		{
			mlbutton_pos = CPoint((LPARAM)::GetMessagePos());
			CRect rc;
			GetWindowRect(&rc);
			if (rc.PtInRect(mlbutton_pos))
			{
				SetCapture();
				mMouseCaptured = TRUE;
				HandlePress();
				InvalidateRect(NULL, false);
				SetTimer(IDT_BUTTON_HELD, 200, nullptr);
			}
		}

		if (WM_TIMER == message)
		{
			if (IDT_REPAINT == wParam)
			{
				KillTimer(wParam);
				InvalidateRect(NULL, false);
				return 0;
			}

			if (IDT_BUTTON_HELD == wParam)
			{
				_ASSERTE(mMouseCaptured);
				int delta = HandlePress();
				if (mLastDelta != delta)
				{
					mLastDelta = delta;
					InvalidateRect(NULL, false);
					UpdateWindow();
				}
				return 0;
			}
		}

		if (message == WM_PAINT)
		{
			DoPaint();
			return 0;
		}

		LRESULT retval = __super::WindowProc(message, wParam, lParam);
		if (message == WM_DESTROY)
			PostMessage(WM_CLOSE);
		return retval;
	}

	void DoPaint()
	{
		CPaintDC pdc(this);
		ThemeUtils::CMemDC hdc(&pdc);

		CRect rect;
		GetClientRect(&rect);
		const int len = rect.bottom - rect.top + 1;
		CRect urect = rect;
		urect.left += 1;
		urect.bottom = urect.top + len / 2 - 1;
		CRect drect = rect;
		drect.left += 1;
		drect.top = drect.top + len / 2;

		CRect wrect = urect;
		ClientToScreen(&wrect);
		bool uhighlighted =
		    !!wrect.PtInRect(CPoint((LPARAM)::GetMessagePos())) && !(::GetAsyncKeyState(VK_LBUTTON) & 0x8000);
		bool upressed = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) && wrect.PtInRect(mlbutton_pos);
		wrect = drect;
		ClientToScreen(&wrect);
		bool dhighlighted =
		    !!wrect.PtInRect(CPoint((LPARAM)::GetMessagePos())) && !(::GetAsyncKeyState(VK_LBUTTON) & 0x8000);
		bool dpressed = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) && wrect.PtInRect(mlbutton_pos);

		const COLORREF bgclr = g_IdeSettings->GetEnvironmentColor(L"DropDownBackground", false);
		const COLORREF borderclr = g_IdeSettings->GetEnvironmentColor(L"DropDownBorder", false);
		const COLORREF borderhighlightclr = g_IdeSettings->GetEnvironmentColor(L"DropDownMouseOverBorder", false);
		const COLORREF buttonclr = g_IdeSettings->GetEnvironmentColor(L"DropDownBackground", false);
		const COLORREF buttonhighlightclr =
		    g_IdeSettings->GetEnvironmentColor(L"ComboBoxButtonMouseOverBackground", false);
		const COLORREF buttonpressedclr =
		    g_IdeSettings->GetEnvironmentColor(L"ComboBoxButtonMouseDownBackground", false);
		const COLORREF arrowclr = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuSubmenuGlyph", false);
		const COLORREF arrowhighlightclr = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseOverGlyph", false);
		const COLORREF arrowpressedclr = g_IdeSettings->GetEnvironmentColor(L"ComboBoxMouseDownGlyph", false);
		static const COLORREF arrowbitmapcolorkey1 = RGB(255, 0, 255);
		static const COLORREF arrowbitmapcolorkey2 = RGB(0, 255, 0);

		hdc.FillSolidRect(&rect, bgclr);
		hdc.SetBkMode(OPAQUE);
		hdc.SelectObject(::GetStockObject(DC_PEN));
		hdc.SetDCPenColor(uhighlighted ? borderhighlightclr : borderclr);
		hdc.SelectObject(::GetStockObject(DC_BRUSH));
		COLORREF ubuttonbackclr = upressed ? buttonpressedclr : (uhighlighted ? buttonhighlightclr : buttonclr);
		COLORREF uarrowclr = upressed ? arrowpressedclr : (uhighlighted ? arrowhighlightclr : arrowclr);
		hdc.SetDCBrushColor(ubuttonbackclr);
		hdc.Rectangle(&urect);

		hdc.SetDCPenColor(dhighlighted ? borderhighlightclr : borderclr);
		COLORREF dbuttonbackclr = dpressed ? buttonpressedclr : (dhighlighted ? buttonhighlightclr : buttonclr);
		COLORREF darrowclr = dpressed ? arrowpressedclr : (dhighlighted ? arrowhighlightclr : arrowclr);
		hdc.SetDCBrushColor(dbuttonbackclr);
		hdc.Rectangle(&drect);

		CDC bdc;
		bdc.CreateCompatibleDC(&hdc);
		static const CSize bitmaps_size(5, 3);

		CBitmap upbitmap;
		if (upbitmap.LoadBitmap(MAKEINTRESOURCE(IDB_SPINNER_UP)))
		{
			ChangeBitmapColour(upbitmap, arrowbitmapcolorkey1, ubuttonbackclr);
			ChangeBitmapColour(upbitmap, arrowbitmapcolorkey2, uarrowclr);
			if (bdc.SelectObject(&upbitmap))
				hdc.BitBlt(urect.CenterPoint().x - bitmaps_size.cx / 2, urect.CenterPoint().y - bitmaps_size.cy / 2,
				           bitmaps_size.cx, bitmaps_size.cy, &bdc, 0, 0, SRCCOPY);
		}

		CBitmap downbitmap;
		if (downbitmap.LoadBitmap(MAKEINTRESOURCE(IDB_SPINNER_DOWN)))
		{
			ChangeBitmapColour(downbitmap, arrowbitmapcolorkey1, dbuttonbackclr);
			ChangeBitmapColour(downbitmap, arrowbitmapcolorkey2, darrowclr);
			if (bdc.SelectObject(&downbitmap))
				hdc.BitBlt(drect.CenterPoint().x - bitmaps_size.cx / 2, drect.CenterPoint().y - bitmaps_size.cy / 2,
				           bitmaps_size.cx, bitmaps_size.cy, &bdc, 0, 0, SRCCOPY);
		}

		if (dhighlighted || uhighlighted)
			SetTimer(IDT_REPAINT, 250, nullptr);
	}

	int HandlePress()
	{
		if (mMouseCaptured)
		{
			CRect rc;
			GetWindowRect(&rc);

			mlbutton_pos = CPoint((LPARAM)::GetMessagePos());
			if (rc.PtInRect(mlbutton_pos))
			{
				CWnd* par = GetParent();
				if (par)
				{
					NMUPDOWN nm;
					nm.hdr.code = UDN_DELTAPOS;
					nm.hdr.idFrom = (UINT)GetDlgCtrlID();
					nm.hdr.hwndFrom = m_hWnd;
					nm.iPos = 0;

					rc.bottom = rc.top + (rc.Height() / 2);
					if (rc.PtInRect(mlbutton_pos))
						nm.iDelta = -1;
					else
						nm.iDelta = 1;

					par->SendMessage(WM_NOTIFY, (WPARAM)GetDlgCtrlID(), (LPARAM)&nm);
					return nm.iDelta;
				}
			}
		}

		return 0;
	}

	void CancelMouseCapture()
	{
		KillTimer(IDT_BUTTON_HELD);
		ReleaseCapture();
		mMouseCaptured = FALSE;
		mlbutton_pos = CPoint(-1, -1);
		mLastDelta = 0;
		InvalidateRect(NULL, false);
		UpdateWindow();
	}

  private:
	CPoint mlbutton_pos;
	BOOL mMouseCaptured = FALSE;
	const UINT_PTR IDT_REPAINT = 225;
	const UINT_PTR IDT_BUTTON_HELD = 226;
	int mLastDelta = 0;
};

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditView

IMPLEMENT_DYNCREATE(CToolTipEditView, CView)

CToolTipEditView::CToolTipEditView()
    : CPartWithRole(CToolTipEditContext::Get()->CreationRole()), 
	m_multiListBox(NULL), mMethodsSpinner(NULL), m_txtList("?"), m_count(0)
{
	
}

CToolTipEditView::~CToolTipEditView()
{
	CatLog("Editor", "~CToolTipEditView");
	delete mMethodsSpinner;
	delete m_multiListBox;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CToolTipEditView, CView)
//{{AFX_MSG_MAP(CToolTipEditView)
ON_WM_CREATE()
ON_WM_SIZE()
ON_WM_PAINT()
ON_CBN_DROPDOWN(100, DoInvalidate)
ON_CBN_SELENDCANCEL(100, DoInvalidate)
ON_WM_KEYDOWN()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditView drawing

void CToolTipEditView::OnDraw(CDC* /*pDC*/)
{
	if (m_multiListBox)
		m_multiListBox->UpdateWindow();
}

int CToolTipEditView::OnCreate(LPCREATESTRUCT pCreateStruc)
{
	if (CView::OnCreate(pCreateStruc) != 0)
		return -1;

	SetFontType(VaFontType::MiniHelpFont);
	mToolTipCtrl.Create(this);
	mToolTipCtrl.Activate(TRUE);
	::mySetProp(mToolTipCtrl.m_hWnd, "__VA_do_not_colour", (HANDLE)1);

	CheckChildren();

	if (m_multiListBox)
		m_multiListBox->SetFontType(VaFontType::MiniHelpFont);

	return 0;
}

// This method exists because under certain circumstances, the IDE deletes some
// of the view child windows.
void CToolTipEditView::CheckChildren(bool sizing /*= false*/)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	auto evctx = CToolTipEditContext::Get();

	if (HasRole(PartRole::Context) && mMethodsSpinner && mMethodsSpinner->m_hWnd && ::IsWindow(mMethodsSpinner->m_hWnd) &&
	    m_multiListBox && m_multiListBox->m_hWnd && ::IsWindow(m_multiListBox->m_hWnd))
	{
		return;
	}
	else if (!HasRole(PartRole::Context) && evctx->ViewListCreated(m_partRole) && m_multiListBox && m_multiListBox->m_hWnd &&
	         ::IsWindow(m_multiListBox->m_hWnd))
	{
		return;
	}

	HWND wnd1 = NULL, wnd2 = NULL;
	{
		CToolTipEditCombo* tmp(m_multiListBox);
		m_multiListBox = NULL;
		if (tmp)
		{
			wnd1 = tmp->GetEditCtrl()->GetSafeHwnd();
			if (HasRole(PartRole::Context))
				wnd2 = tmp->GetComboBoxCtrl()->GetSafeHwnd();
			tmp->DestroyWindow();
			delete tmp;
		}
	}

	CToolInfo ti;
	ZeroMemory(&ti, sizeof(CToolInfo));
	ti.cbSize = sizeof(TOOLINFO);

	if (wnd1)
	{
		ti.hwnd = wnd1;
		ti.uFlags = TTF_IDISHWND;
		ti.uId = (UINT_PTR)wnd1;
		mToolTipCtrl.SendMessage(TTM_DELTOOL, 0, (LPARAM)&ti);
	}

	if (wnd2)
	{
		ti.hwnd = wnd2;
		ti.uFlags = TTF_IDISHWND;
		ti.uId = (UINT_PTR)wnd2;
		mToolTipCtrl.SendMessage(TTM_DELTOOL, 0, (LPARAM)&ti);
	}

	if (mMethodsSpinner)
	{
		ti.hwnd = mMethodsSpinner->m_hWnd;
		ti.uFlags = 0;
		ti.uId = 1;
		mToolTipCtrl.SendMessage(TTM_DELTOOL, 0, (LPARAM)&ti);
		ti.uId = 2;
		mToolTipCtrl.SendMessage(TTM_DELTOOL, 0, (LPARAM)&ti);
	}

	CRect rc;
	m_multiListBox = new CToolTipEditCombo(m_partRole);
	if (HasRole(PartRole::Context))
	{
		::GetWindowRect(::GetParent(::GetParent(::GetParent(m_hWnd))), &rc);

		if (Psettings->minihelpAtTop)
		{
			rc.top = rc.top + (int)Psettings->m_minihelpHeight;
			// gmit: 67174: GetDesktopEdges already works satisfactorily
			rc.bottom = g_FontSettings->GetDesktopEdges(g_currentEdCnt->GetSafeHwnd()).bottom;
		}
		else
		{
			rc.top = 0;
			rc.bottom = rc.bottom - (int)Psettings->m_minihelpHeight;
		}

		rc.right -= BUTTONWIDTH;
		evctx->ViewListCreated(PartRole::Context) = true;
		evctx->ViewListCreated(PartRole::Definition) = false;
		evctx->ViewListCreated(PartRole::Project) = false;
		m_multiListBox->Create(WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		                           CBS_AUTOHSCROLL | CBS_DROPDOWN | CBS_SORT,
		                       rc, this, 100);

		rc.left = rc.right;
		rc.right += BUTTONWIDTH;
		CWnd* tmp(mMethodsSpinner);
		mMethodsSpinner = NULL;

		if (CVS2010Colours::IsVS2010NavBarColouringActive() && CVS2010Colours::IsExtendedThemeActive())
		{
			SpinnerV11* s = new SpinnerV11;
			s->Create(GetDefaultVaWndCls(), "Spin", WS_CLIPSIBLINGS | WS_VISIBLE | WS_CHILD /*|UDS_HORZ*/, rc, this,
			          102);
			mMethodsSpinner = s;
		}
		else
		{
			SpinnerOld* s = new SpinnerOld;
			s->Create(WS_CLIPSIBLINGS | WS_VISIBLE | WS_CHILD /*|UDS_HORZ*/, rc, this, 102);
			mMethodsSpinner = s;
			mMethodsSpinner->SetWindowText("Spin");
		}

		delete tmp;
		if (!sizing && ::IsWindow(m_hWnd))
		{
			GetWindowRect(&rc);
			MoveWindowIfNeeded(mMethodsSpinner, rc.Width() - (BUTTONWIDTH), 0, BUTTONWIDTH, rc.Height());
			rc.right -= BUTTONWIDTH;
		}
	}
	else
	{
		GetWindowRect(&rc);
		evctx->ViewListCreated(m_partRole) = true;
		m_multiListBox->Create(WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
		                           CBS_AUTOHSCROLL | CBS_DROPDOWN | CBS_SORT,
		                       rc, this, 102);
	}

	if (!sizing && ::IsWindow(m_hWnd))
	{
		int cx = rc.Width() + 4;
		if (HasRole(PartRole::Context) && CVS2010Colours::IsVS2010NavBarColouringActive())
			cx -= 2; // gmit: combo overlaps with splitter! (see also OnSize)
		MoveWindowIfNeeded(m_multiListBox, -2, -2, cx, VsUI::DpiHelper::LogicalToDeviceUnitsY(100));
		Invalidate(FALSE);
	}

	gImgListMgr->SetImgListForDPI(*m_multiListBox, ImageListManager::bgCombo);

	// tag for identification as Minihelp editCtrl
	CEdit* pEd = m_multiListBox->GetEditCtrl();
	if (pEd->GetSafeHwnd())
	{
		_ASSERTE(!::GetWindowLongPtr(*pEd, GWLP_USERDATA));
		const int kMinihelpMagicData = 0xae0ae0;
		::SetWindowLongPtr(*pEd, GWLP_USERDATA, kMinihelpMagicData);

		ZeroMemory(&ti, sizeof(CToolInfo));
		ti.cbSize = sizeof(TOOLINFO);

		if (HasRole(PartRole::Context))
		{
			ti.hwnd = pEd->m_hWnd;
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_CENTERTIP;
			ti.uId = (UINT_PTR)pEd->m_hWnd;
			static const char* kTxt1 = "Context";
			ti.lpszText = const_cast<char*>(kTxt1);
			mToolTipCtrl.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);

			ti.hwnd = m_multiListBox->GetComboBoxCtrl()->m_hWnd;
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			ti.uId = (UINT_PTR)m_multiListBox->GetComboBoxCtrl()->m_hWnd;

			static char kTxt2[255];
			const WTString binding = ::GetBindingTip("VAssistX.ListMethodsInCurrentFile", "Alt+M");
			sprintf(kTxt2, "List of Methods in Current File%s", binding.c_str());
			ti.lpszText = kTxt2;
			mToolTipCtrl.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
		}
		else
		{
			ti.hwnd = pEd->m_hWnd;
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_CENTERTIP;
			ti.uId = (UINT_PTR)pEd->m_hWnd;
			static const char* kTxt1 = "Definition";
			ti.lpszText = const_cast<char*>(kTxt1);
			mToolTipCtrl.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
		}
	}

	if (mMethodsSpinner)
	{
		CRect r;
		mMethodsSpinner->GetWindowRect(&r);
		ti.hwnd = mMethodsSpinner->m_hWnd;
		ti.uFlags = TTF_SUBCLASS;

		static char txt[255];
		WTString binding = ::GetBindingTip("VAssistX.ScopePrevious", "Alt+Up");
		sprintf(txt, "Previous Method/Scope in file%s", binding.c_str());
		ti.lpszText = txt;
		ti.uId = 1;
		ti.rect = CRect(0, 0, r.Width(), r.Height() / 2);
		mToolTipCtrl.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);

		static char txt2[255];
		binding = ::GetBindingTip("VAssistX.ScopeNext", "Alt+Down");
		sprintf(txt2, "Next Method/Scope in file%s", binding.c_str());
		ti.lpszText = txt2;
		ti.uId = 2;
		ti.rect = CRect(0, r.Height() / 2, r.Width(), r.Height());
		mToolTipCtrl.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	}
}

void CToolTipEditView::OnSize(UINT nType, int cx, int cy)
{
	auto dpiScope = SetDefaultDpiHelper();

	try
	{
		CView::OnSize(nType, cx, cy);
		if (::IsWindow(m_hWnd))
		{
			CheckChildren(true);
			if (HasRole(PartRole::Context))
			{
				MoveWindowIfNeeded(mMethodsSpinner, cx - (BUTTONWIDTH), 0, BUTTONWIDTH, cy);
				cx -= (BUTTONWIDTH);
				if (CVS2010Colours::IsVS2010NavBarColouringActive())
					cx -= 2; // gmit: combo overlaps with splitter! (see also CheckChildren)
			}
			MoveWindowIfNeeded(m_multiListBox, -2, -2, cx + 4, VsUI::DpiHelper::LogicalToDeviceUnitsY(100));
		}
		Invalidate(FALSE);
	}
	catch (...)
	{
		
	}
}

void CToolTipEditView::OnPaint()
{
	if (CVS2010Colours::IsVS2010NavBarColouringActive() && CVS2010Colours::IsExtendedThemeActive())
	{
		CheckChildren();
		CView::OnPaint();
	}
	else
	{
		ValidateRect(NULL);
		CheckChildren();
		if (HasRole(PartRole::Context))
			mMethodsSpinner->RedrawWindow();
		m_multiListBox->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
	}
}

void CToolTipEditView::DoInvalidate()
{
	Invalidate(TRUE);
}

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditView diagnostics

#ifdef _DEBUG
void CToolTipEditView::AssertValid() const
{
	CView::AssertValid();
}

void CToolTipEditView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}
#endif //_DEBUG

void CToolTipEditView::OnDpiChanged(CWndDpiAware::DpiChange change, bool& handled)
{
	if (change == CWndDpiAware::DpiChange::AfterParent)
	{
		SettingsChanged();
	}

	__super::OnDpiChanged(change, handled);
}

/////////////////////////////////////////////////////////////////////////////
// CToolTipEditView message handlers
void CToolTipEditView::SetWindowText(LPCWSTR lpszString, bool isColorableText)
{
	EdCntPtr ed(g_currentEdCnt);
	try
	{
		if (!ed || !IsWindow(ed->m_hWnd))
			return;
		if (m_multiListBox && m_multiListBox->GetDroppedState())
			return;
		CheckChildren();
		if (!m_multiListBox || m_multiListBox->GetDroppedState())
			return;
	}
	catch (...)
	{
		VALOGEXCEPTION("TTEV:");
		return;
	}

	const CStringW inString(lpszString);

	if (HasRole(PartRole::Project))
	{
		m_multiListBox->ResetContent();

		int count = m_multiListBox->GetCount();
		for (int i = 0; i < count; i++)
			m_multiListBox->DeleteItem(0);

		COMBOBOXEXITEMW cbi;
		memset(&cbi, 0, sizeof(cbi));
		cbi.iImage = ed->ProjectIconIdx();
		cbi.pszText = (LPWSTR)(LPCWSTR)inString;
		cbi.cchTextMax = inString.GetLength();
		cbi.mask = CBEIF_IMAGE | CBEIF_INDENT | CBEIF_SELECTEDIMAGE | CBEIF_TEXT;
		cbi.iSelectedImage = cbi.iImage;
		cbi.iIndent = 0; // Set indentation according

		m_multiListBox->SetColorableText(isColorableText);
		m_multiListBox->SendMessage(CBEM_INSERTITEMW, 0, (LPARAM)&cbi);
		m_multiListBox->SetCurSel(0);
		if (m_multiListBox->GetEditCtrl())
			m_multiListBox->GetEditCtrl()->SetSel(0, 0);
		m_multiListBox->RedrawWindow();
	}

	if (m_txtList == inString)
		return;

	CStringW tmpTxt(inString);
	tmpTxt += L'\n';
	m_multiListBox->SetColorableText(isColorableText);

	if (!HasRole(PartRole::Context) || inString.IsEmpty())
	{
		m_multiListBox->ResetContent();

		int count = m_multiListBox->GetCount();
		for (int i = 0; i < count; i++)
			m_multiListBox->DeleteItem(0);
	}

	CStringW item;
	COMBOBOXEXITEMW cbi;
	LPWSTR ptr = tmpTxt.GetBuffer(0);
	int cnt = 0;
	static BOOL isScope = FALSE;
	for (wchar_t* idx; ptr && (idx = wcschr(ptr, L'\n')) != NULL; ptr = idx + 1)
	{
		*idx = L'\0';
		if (*ptr == L' ')
			ptr++;
		item = ptr;
		if (item.IsEmpty() || item.Find(L"HIDETHIS") != -1)
			continue;

		ZeroMemory(&cbi, sizeof(COMBOBOXEXITEMW));
		cbi.mask = CBEIF_IMAGE | CBEIF_INDENT | CBEIF_SELECTEDIMAGE | CBEIF_TEXT;
		cbi.iItem = cnt++;
		cbi.pszText = (LPWSTR)(LPCWSTR)item;
		cbi.cchTextMax = item.GetLength();
		DType dt = ed->GetSymDtype();
		if (HasRole(PartRole::Context))
		{
			if (ed->GetSymDef().GetLength() > 1 && !ed->HasSelection())
			{
				isScope = FALSE;
				if (dt.IsSysLib())
					cbi.iImage = ICONIDX_SCOPESYS;
				else if (dt.inproject() && !dt.infile())
					cbi.iImage = ICONIDX_SCOPEPROJECT;
				else
					cbi.iImage = ICONIDX_SCOPELOCAL;
			}
			else
			{
				// context scope, class:method:if:...
				// strip off :if:... and get
				isScope = FALSE;
				WTString def;
				WTString scope = ed->m_lastScope;
				if (scope.GetLength())
					scope = scope.Mid(0, scope.GetLength() - 1);
				int type, attrs;
				type = attrs = 0;
				MultiParsePtr mp(ed->GetParseDb());
				if (mp)
					mp->Tdef(scope, def, FALSE, type, attrs);
				if (!type && !inString.IsEmpty() && dt.HasFileFlag())
				{
					if (dt.IsSysLib())
						cbi.iImage = ICONIDX_SCOPESYS;
					else if (dt.inproject())
						cbi.iImage = ICONIDX_SCOPEPROJECT;
					else
						cbi.iImage = ICONIDX_SCOPECUR; // file without proper attributes?
				}
				else
				{
					cbi.iImage = ICONIDX_SCOPECUR;
					isScope = TRUE;
				}
			}
		}
		else if (HasRole(PartRole::Project))
		{
			cbi.iImage = ed->ProjectIconIdx();
		}
		else if (isScope)
			cbi.iImage = ICONIDX_SCOPECUR;
		else
		{
			if (dt.HasFileFlag())
				cbi.iImage = GetFileImgIdx(inString);
			else
				cbi.iImage = GetTypeImgIdx(dt.MaskedType(), dt.Attributes());
		}
		cbi.iSelectedImage = cbi.iImage;
		cbi.iIndent = 0; // Set indentation according
		m_multiListBox->SendMessage(CBEM_INSERTITEMW, 0, (LPARAM)&cbi);
		if (m_multiListBox->GetEditCtrl())
			SetWindowTextW(m_multiListBox->GetEditCtrl()->GetSafeHwnd(), item);
	}

	CRect rc;
	GetWindowRect(&rc);
	m_count = cnt;
	tmpTxt.ReleaseBuffer(-1);
	m_txtList = inString;
	m_multiListBox->SetCurSel(0);
	if (m_multiListBox->GetEditCtrl())
		m_multiListBox->GetEditCtrl()->SetSel(0, 0);
	m_multiListBox->RedrawWindow();
}

BOOL CToolTipEditView::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	NMHDR* pNMHDR = (NMHDR*)lParam;
	if (pNMHDR->code == UDN_DELTAPOS)
	{
		LPNMUPDOWN pd = (LPNMUPDOWN)lParam;
		if (g_currentEdCnt)
			g_currentEdCnt->GotoNextMethodInFile(pd->iDelta > 0);
	}
	return CWnd::OnNotify(wParam, lParam, pResult);
}

CComboBox* CToolTipEditView::GetComboBox()
{
	if (m_multiListBox)
	{
		return m_multiListBox->GetComboBoxCtrl();
	}
	return nullptr;
}

void CToolTipEditView::SettingsChanged()
{
	if (m_multiListBox)
	{
		gImgListMgr->SetImgListForDPI(*m_multiListBox, ImageListManager::bgCombo);
		m_multiListBox->SettingsChanged();
		m_multiListBox->SetFontType(VaFontType::MiniHelpFont);
	}

	SetFontType(VaFontType::MiniHelpFont);
}

bool CToolTipEditView::HasColorableText() const
{
	if (m_multiListBox)
		return m_multiListBox->HasColorableText();
	return false;
}

BOOL CToolTipEditView::PreCreateWindow(CREATESTRUCT& cs)
{
	if (CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		cs.lpszClass = ::GetDefaultVaWndCls();
		cs.style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}

	return __super::PreCreateWindow(cs);
}

CToolTipEditContext* CToolTipEditContext::Get()
{
	static CToolTipEditContext ctx;
	return &ctx;
}
