// NonViewSplitter.cpp : implementation file
//

#include "stdafx.h"
#include "NonViewSplitter.h"
#include "MiniHelpFrm.h"
#include "..\DevShellAttributes.h"
#include "..\ColorListControls.h"
#include "WindowUtils.h"
#include "EDCNT.H"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern const UINT WM_UPDATE_MY_BORDER = ::RegisterWindowMessage("WM_UPDATE_MY_BORDER");
static CWnd* FindCombo(HWND pane);

/////////////////////////////////////////////////////////////////////////////
// NonViewSplitter

NonViewSplitter::NonViewSplitter()
    : m_parent(NULL)
{

}

NonViewSplitter::~NonViewSplitter()
{
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(NonViewSplitter, CSplitterWnd)
//{{AFX_MSG_MAP(NonViewSplitter)
// NOTE - the ClassWizard will add and remove mapping macros here.
//}}AFX_MSG_MAP
ON_REGISTERED_MESSAGE(WM_UPDATE_MY_BORDER, OnUpdateMyBorder)
ON_WM_DRAWITEM()
END_MESSAGE_MAP()
#pragma warning(pop)


/////////////////////////////////////////////////////////////////////////////
// NonViewSplitter message handlers

BOOL NonViewSplitter::CreateMinihelp(MiniHelpFrm* pParentWnd)
{
	m_parent = pParentWnd;

	int cols[2] = {0, 0}; 
	int rslt = sscanf_s(Psettings->m_minihelpInfo, "%d;%d", &cols[0], &cols[1]);
	if (rslt == 2)
	{
		m_colProps.resize(2);
		m_colProps[0] = (float)cols[0] / 1000;
		m_colProps[1] = (float)cols[1] / 1000;
	}
	else
	{
		m_colProps.resize(2);
		m_colProps[0] = 0.4f;
		m_colProps[1] = 0.8f;		
	}

	DpiScaleFields();

	return CSplitterWnd::CreateStatic((CWnd*)pParentWnd, 1, 3, WS_CHILD | WS_VISIBLE);
}

bool NonViewSplitter::IsLayoutValid()
{
	for (int col = 0; col < m_nCols; col++)
	{
		for (int row = 0; row < m_nRows; row++)
		{
			if (!GetDlgItem(IdFromRowCol(row, col)))
			{
				return false;
			}
		}
	}

	return true;
}

void NonViewSplitter::GetSizes(std::vector<int>& columns, CSize& size) const
{
	int cols = GetColumnCount();
	columns.resize((size_t) cols);
	int tmp;
	for (int i = 0; i < cols; i++)
		GetColumnInfo(i, columns[(size_t)i], tmp);
	CRect inside;
	GetInsideRect(inside);
	size = inside.Size();
}

void NonViewSplitter::DpiScaleFields()
{
	static int default_cxSplitter = m_cxSplitter;
	static int default_cxSplitterGap = m_cxSplitterGap;

	if (m_parent)
	{
		if (gShellAttr->IsDevenv10OrHigher())
		{
			auto dpiScope = m_parent->SetDefaultDpiHelper();
			int cx = VsUI::DpiHelper::LogicalToDeviceUnitsX(5);
			m_cxSplitter = std::max({cx, default_cxSplitter});
			m_cxSplitterGap = std::max({cx, default_cxSplitterGap});
		}

		// other controls are not ready yet
 		
//		m_cxBorder = VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
// 		m_cySplitter = VsUI::DpiHelper::LogicalToDeviceUnitsY(3 + 2 + 2);
// 		m_cySplitterGap = VsUI::DpiHelper::LogicalToDeviceUnitsY(3 + 2 + 2);
// 		m_cyBorder = VsUI::DpiHelper::LogicalToDeviceUnitsY(2);	
	}
}

bool NonViewSplitter::HaveSizesChanged()
{
	std::vector<int> columns;
	CSize size;
	GetSizes(columns, size);

	if (size != last_size || columns.size() != last_columns.size())
		return true;

	for (size_t i = 0; i < columns.size(); i++)
		if (columns[i] != last_columns[i])
			return true;

	return false;
}

void NonViewSplitter::StopTracking(BOOL bAccept)
{
	TrackingCounter _tr(m_afterTracking);
	ReleaseCapture();
	m_changedCol = -1;
	m_trackingCol = -1;
	__super::StopTracking(bAccept);
	if (bAccept)
		SetProps();
	m_changedCol = -1;
	if (m_tracker.m_hWnd && bAccept)
	{
		m_tracker.ShowWindow(SW_HIDE);
	}

	if (bAccept)
	{
		EdCntPtr ed(g_currentEdCnt);
		if (ed)
			ed->vSetFocus();
	}
}

void NonViewSplitter::TrackColumnSize(int x, int col)
{
	CPoint pt(x, 0);
	ClientToScreen(&pt);
	GetPane(0, col)->ScreenToClient(&pt);

	if (col + 1 == GetColumnCount())
		m_pColInfo[col].nIdealSize = pt.x; // new size
	else
	{
		int sum_size =
		    m_pColInfo[col].nIdealSize +
		    m_pColInfo[col + 1].nIdealSize;

		m_pColInfo[col].nIdealSize = pt.x;
		m_pColInfo[col + 1].nIdealSize = sum_size - pt.x;
	}

	m_changedCol = col;
}

void NonViewSplitter::SaveSettings() const
{
	int cols[2] = {
		(int)(m_colProps[0] * 1000), 
		(int)(m_colProps[1] * 1000)
	};

	sprintf_s(Psettings->m_minihelpInfo, "%d;%d", cols[0], cols[1]);
}

void NonViewSplitter::OnInvertTracker(const CRect& rect)
{
	InitTracker();

	if (m_useTracker && m_tracker.m_hWnd)
		m_tracker.MoveWindow(&rect);

	if (m_directTracking && m_trackingCol >= 0)
	{
		int col = m_trackingCol;
	
		CRect rc(rect);
		ClientToScreen(&rc);
		GetPane(0, col)->ScreenToClient(&rc);
	
		int x = rc.left - m_cxSplitterGap/2 + m_cxBorder;

		if (col + 1 == GetColumnCount())
			m_pColInfo[col].nIdealSize = x; // new size
		else
		{
			int sum_size =
			    m_pColInfo[col].nIdealSize +
			    m_pColInfo[col + 1].nIdealSize;

			m_pColInfo[col].nIdealSize = x;
			m_pColInfo[col + 1].nIdealSize = sum_size - x;
		}
		CSplitterWnd::RecalcLayout();
	}

	if (m_useTracker || m_directTracking)
	{
		Invalidate();
		RedrawWindow(NULL, NULL, RDW_ALLCHILDREN | RDW_UPDATENOW);	
	}
}

void NonViewSplitter::StartTracking(int ht)
{
	InitTracker();

	if (ht)
	{
		m_trackingCol = -1;

		CPoint pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);

		int minDist = INT_MAX;

		int sum = 0;
		int colCount = GetColumnCount();
		for (int i = 0; i < colCount - 1; i++)
		{
			sum += m_pColInfo[i].nCurSize;
			int dist = std::abs(pt.x - sum);
			if (dist < minDist)
			{
				m_trackingCol = i;
				minDist = dist;
			}
		}
	}

	__super::StartTracking(ht);
}

void NonViewSplitter::InitTracker()
{
	if (!m_useTracker)
		return;

	if (m_tracker.m_hWnd)
	{
		if (!m_tracker.IsWindowVisible())
			m_tracker.ShowWindow(SW_SHOW);
		
		return;
	}

	CRect rc;
	m_tracker.Create("", SS_OWNERDRAW | WS_CHILD | WS_VISIBLE | WS_EX_LAYERED, rc, this);
	if (m_tracker.m_hWnd)
	{
		::SetLayeredWindowAttributes(m_tracker.m_hWnd, 0, 0x7f, LWA_ALPHA);
		::SetWindowPos(m_tracker.m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
	}
}

void NonViewSplitter::OnDpiChanged(DpiChange change, bool& handled)
{
	__super::OnDpiChanged(change, handled);
	if (change == DpiChange::AfterParent)
	{
		DpiScaleFields();
		SettingsChanged();
	}
}

void NonViewSplitter::SettingsChanged()
{
	FitColumnsToWidth();
	RecalcLayout(false);
}

void NonViewSplitter::FitColumnsToWidth()
{
	if (m_afterTracking)
	{
		SetProps();

		if (GetCapture() == this)
			ReleaseCapture();
	}

	if (m_colProps.empty())
		return;

	if (m_hWnd && ::IsWindow(m_hWnd))
	{
		auto dpiScope = SetDefaultDpiHelper();

		CRect rect;
		GetClientRect(rect);

		int totalWidth = rect.Width();
		if (totalWidth <= 0)
			return;

		int numCols = GetColumnCount();
		if (numCols < 2 || numCols <= (int)m_colProps.size())
			return;

		int minWidth = VsUI::DpiHelper::LogicalToDeviceUnitsX(100);
		int collapseWidth = 0; //VsUI::DpiHelper::LogicalToDeviceUnitsX(50);
		int colSum = 0;

		std::vector<int> colWidths;
		colWidths.resize((size_t)numCols);

		for (int i = 0; i < numCols - 1; ++i)
		{
			int pos = (int)(m_colProps[(size_t)i] * (float)totalWidth);

			int colWidth = pos - colSum;

			colSum += colWidth;
			colWidths[(size_t)i] = colWidth;
		}

		colWidths[(size_t)(numCols - 1)] = totalWidth - colSum;
		int fixedCount = 0;

		// apply minimum width
		for (int i = 0; i < numCols; i++)
		{
			if (colWidths[(size_t)i] < minWidth)
			{
				colWidths[(size_t)i] = minWidth;
				fixedCount++;
			}
		}

		if (m_afterTracking && fixedCount == 0)
			return;

		// calculate whether we can fit the width
		colSum = std::accumulate(colWidths.cbegin(), colWidths.cend(), 0);
		if (colSum != totalWidth)
		{
			double ratio = static_cast<double>(totalWidth) / colSum;
			colSum = 0;
			for (int i = 0; i < numCols - 1; i++)
			{
				colWidths[(size_t)i] = WindowScaler::Scale<int>(colWidths[(size_t)i], ratio);
				colSum += colWidths[(size_t)i];
			}
			colWidths[(size_t)(numCols - 1)] = totalWidth - colSum;
		}

		// set columns widths 
		for (int i = 0; i < numCols; i++)
			SetColumnInfo(i, colWidths[(size_t)i], collapseWidth);

		int rowHeight = VsUI::DpiHelper::LogicalToDeviceUnitsY((int)Psettings->m_minihelpHeight);
		SetRowInfo(0, rowHeight, rowHeight);
	}
}

bool NonViewSplitter::SetProps()
{
	if (!HaveSizesChanged())
		return false;

	int numCols = GetColumnCount();
	if (numCols < 2)
		return false;

	// invalid state, we need to initialize
	if (m_changedCol >= 0 && m_colProps.empty())
		m_changedCol = -1;

	m_colProps.resize((size_t)(numCols - 1));

	int totalCurrentWidth = 0;
	int curWidth;
	int curMinWidth;

	for (int i = 0; i < numCols; ++i)
	{
		GetColumnInfo(i, curWidth, curMinWidth);
		totalCurrentWidth += curWidth;
	}

	int currSum = 0;
	for (int i = 0; i < numCols - 1; ++i)
	{
		GetColumnInfo(i, curWidth, curMinWidth);
		currSum += curWidth;

		if (m_changedCol < 0)
			m_colProps[(size_t)i] = (float)currSum / (float)totalCurrentWidth;
		else if (m_changedCol == i)
		{
			m_colProps[(size_t)i] = (float)currSum / (float)totalCurrentWidth;
			break;
		}
	}

	SaveSettings();	
	GetSizes(last_columns, last_size);
	return true;
}

void NonViewSplitter::RecalcLayout(bool parent)
{
	if (IsLayoutValid())
	{
		FitColumnsToWidth();
		CSplitterWnd::RecalcLayout();
		RedrawWindow();

// 		if (m_afterTracking && m_nCols && m_nRows)
// 		{
// 			SaveColumns();
// 		}

		if (m_colProps.empty())
		{
			m_changedCol = -1;
			SetProps();
		}

		// maybe save width of context and update all editparentwnds	
	}

	if (GetCapture() == this)
		ReleaseCapture();
}

BOOL NonViewSplitter::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	ASSERT(FALSE); // does this ever happen??
	if (CWnd::OnNotify(wParam, lParam, pResult))
		return TRUE;

	// route commands to the splitter to the parent frame window
	// fix crash on Kieth's machine - GetParentFrame was null - don't
	//  know why this fn was ever called in the first place though
	CFrameWnd* pParent = GetParentFrame();
	if (pParent)
		*pResult = pParent->SendMessage(WM_NOTIFY, wParam, lParam);
	else
		*pResult = 0;
	return TRUE;
}
BOOL NonViewSplitter::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return CWnd::OnCommand(wParam, lParam);
}
//#include <../src/afximpl.h>
// void NonViewSplitter::OnPaint()
//{
//	ASSERT_VALID(this);
//	CPaintDC dc(this);
//
//	CRect rectClient;
//	GetClientRect(&rectClient);
//	rectClient.InflateRect(-m_cxBorder, -m_cyBorder);
//
//	CRect rectInside;
//	GetInsideRect(rectInside);
//
//	// draw the splitter boxes
//	if (m_bHasVScroll && m_nRows < m_nMaxRows)
//	{
//		OnDrawSplitter(&dc, splitBox,
//			CRect(rectInside.right + afxData.bNotWin4, rectClient.top,
//			rectClient.right, rectClient.top + m_cySplitter));
//	}
//
//	if (m_bHasHScroll && m_nCols < m_nMaxCols)
//	{
//		OnDrawSplitter(&dc, splitBox,
//			CRect(rectClient.left, rectInside.bottom + afxData.bNotWin4,
//			rectClient.left + m_cxSplitter, rectClient.bottom));
//	}
//
//	// extend split bars to window border (past margins)
//	DrawAllSplitBars(&dc, rectInside.right, rectInside.bottom);
//
//	if (!afxData.bWin4)
//	{
//		// draw splitter intersections (inside only)
//		GetInsideRect(rectInside);
//		dc.IntersectClipRect(rectInside);
//		CRect rect;
//		rect.top = rectInside.top;
//		for (int row = 0; row < m_nRows - 1; row++)
//		{
//			rect.top += m_pRowInfo[row].nCurSize + m_cyBorderShare;
//			rect.bottom = rect.top + m_cySplitter;
//			rect.left = rectInside.left;
//			for (int col = 0; col < m_nCols - 1; col++)
//			{
//				rect.left += m_pColInfo[col].nCurSize + m_cxBorderShare;
//				rect.right = rect.left + m_cxSplitter;
//				OnDrawSplitter(&dc, splitIntersection, rect);
//				rect.left = rect.right + m_cxBorderShare;
//			}
//			rect.top = rect.bottom + m_cxBorderShare;
//		}
//	}
//}
#include "../Settings.h"

static CWnd* FindCombo(HWND pane)
{
	CWnd* comboex = CWnd::FindWindowEx(pane, NULL, WC_COMBOBOXEXA, NULL);
	if (!comboex)
		return NULL;
	return CWnd::FindWindowEx(comboex->m_hWnd, NULL, WC_COMBOBOXA, NULL);
}

void NonViewSplitter::OnDrawSplitter(CDC* pDC, ESplitType nType, const CRect& rectArg)
{
	if (!gShellAttr->IsDevenv())
	{
		CSplitterWnd::OnDrawSplitter(pDC, nType, rectArg);
		return;
	}

	// if pDC == NULL, then just invalidate
	if (pDC == NULL)
	{
		RedrawWindow(rectArg, NULL, RDW_INVALIDATE | RDW_NOCHILDREN);
		return;
	}
	ASSERT_VALID(pDC);

	// otherwise, actually draw
	COLORREF borderColor = ::GetSysColor(COLOR_3DSHADOW);
	COLORREF clr;
	if (CVS2010Colours::IsVS2010NavBarColouringActive())
		clr = GetVS2010VANavBarBkColour();
	else
		clr = ::GetSysColor(COLOR_3DFACE);
	CRect rect = rectArg;
	CRect rectOuter = rectArg;
	// 	rect.DeflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), VsUI::DpiHelper::LogicalToDeviceUnitsY(1));
	rect.DeflateRect(1, 1);

	switch (nType)
	{
	case splitBorder:
		//		ASSERT(afxData.bWin4);
		//		pDC->Draw3dRect(rect, afxData.clrBtnShadow, afxData.clrBtnHilite);
		//		rect.InflateRect(-CX_BORDER, -CY_BORDER);
		//		pDC->Draw3dRect(rect, afxData.clrWindowFrame, afxData.clrBtnFace);

		if (CVS2010Colours::IsVS2010NavBarColouringActive())
		{
			for (int i = 0; i < GetColumnCount(); i++)
			{
				CWnd* pane = GetPane(0, i);
				if (!pane)
					continue;
				CRect prect;
				pane->GetWindowRect(prect);
				ScreenToClient(prect);
				if (rectArg.PtInRect(prect.TopLeft()) && rectArg.PtInRect(prect.BottomRight()))
				{
					CWnd* combo = FindCombo(pane->m_hWnd);
					if (combo)
					{
						extern const UINT WM_DRAW_MY_BORDER;
						if (combo->SendMessage(WM_DRAW_MY_BORDER, (WPARAM)pDC, (LPARAM)&rectOuter))
							return;
					}
					break;
				}
			}
		}

		pDC->Draw3dRect(rectOuter, clr, clr);
		pDC->Draw3dRect(rect, borderColor, borderColor);
		return;
	case splitIntersection:
		//		ASSERT(!afxData.bWin4);
		break;

	case splitBox:
		//		if (afxData.bWin4)
		{
			//			pDC->Draw3dRect(rect, afxData.clrBtnFace, afxData.clrWindowFrame);
			//			rect.InflateRect(-CX_BORDER, -CY_BORDER);
			//			pDC->Draw3dRect(rect, afxData.clrBtnHilite, afxData.clrBtnShadow);
			//			rect.InflateRect(-CX_BORDER, -CY_BORDER);
			pDC->Draw3dRect(rectOuter, clr, clr);
			pDC->Draw3dRect(rect, borderColor, borderColor);
			break;
		}
		// fall through...
	case splitBar:
		//		if (!afxData.bWin4)
		{
			//			pDC->Draw3dRect(rect, afxData.clrBtnHilite, afxData.clrBtnShadow);
			//			rect.InflateRect(-CX_BORDER, -CY_BORDER);
			pDC->Draw3dRect(rect, borderColor, borderColor);
			pDC->Draw3dRect(rectOuter, clr, clr);
		}
		break;

	default:
		ASSERT(FALSE); // unknown splitter type
	}

	// fill the middle
	pDC->FillSolidRect(rect, clr);
}

void NonViewSplitter::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (lpDrawItemStruct && lpDrawItemStruct->hDC &&
	    m_tracker.m_hWnd && m_tracker.GetDlgCtrlID() == nIDCtl)
	{
		CBrush* halftoneBrush = CDC::GetHalftoneBrush();
		if (halftoneBrush)
		{
			::FillRect(lpDrawItemStruct->hDC, &lpDrawItemStruct->rcItem, *halftoneBrush);
		}
	}		
}

LRESULT NonViewSplitter::OnUpdateMyBorder(WPARAM wparam, LPARAM lparam)
{
	if (!CVS2010Colours::IsVS2010NavBarColouringActive())
		return 0;

	HWND combohwnd = (HWND)wparam;
	for (int i = 0; i < GetColumnCount(); i++)
	{
		CWnd* pane = GetPane(0, i);
		if (!pane)
			continue;
		CWnd* combo = FindCombo(pane->m_hWnd);
		if (combo && (combo->m_hWnd == combohwnd))
		{
			CRect rect;
			pane->GetWindowRect(rect);
			ScreenToClient(rect);
			rect.InflateRect(m_cxBorder, m_cyBorder, m_cxBorder, m_cyBorder);
			// 			rect.InflateRect(VsUI::DpiHelper::LogicalToDeviceUnitsX(2),
			// VsUI::DpiHelper::LogicalToDeviceUnitsY(2), VsUI::DpiHelper::LogicalToDeviceUnitsX(2),
			// VsUI::DpiHelper::LogicalToDeviceUnitsX(2));
			//			InvalidateRect(rect);
			CWindowDC dc(this);
			OnDrawSplitter(&dc, splitBorder, rect);
			return 1;
		}
	}
	return 0;
}

BOOL NonViewSplitter::PreCreateWindow(CREATESTRUCT& cs)
{
	if (CVS2010Colours::IsVS2010NavBarColouringActive())
	{
		cs.lpszClass = ::GetDefaultVaWndCls();
		cs.style |= WS_CLIPSIBLINGS;
	}

	return __super::PreCreateWindow(cs);
}
