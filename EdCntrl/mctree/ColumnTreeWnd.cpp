/*********************************************************************
* Multi-Column Tree View, version 1.4 (July 7, 2005)
* Copyright (C) 2003-2005 Michal Mecinski.
*
* You may freely use and modify this code, but don't remove
* this copyright note.
*
* THERE IS NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, FOR
* THIS CODE. THE AUTHOR DOES NOT TAKE THE RESPONSIBILITY
* FOR ANY DAMAGE RESULTING FROM THE USE OF IT.
*
* E-mail: mimec@mimec.org
* WWW: http://www.mimec.org
********************************************************************/

#include "stdafxed.h"
#include "ColumnTreeWnd.h"

#include <shlwapi.h>
#include "..\Settings.h"
#include "..\SaveDC.h"
#include "..\MenuXP\MenuXP.h"
#include "..\tree_iterator.h"
#define LOCK_WINDOW_IMPLEMENT
#include "..\LockWindow.h"
#include "..\FontSettings.h"
#include "..\SyntaxColoring.h"
#include "vsshell100.h"
#include "..\DevShellAttributes.h"
#include "..\ColorSyncManager.h"
#include "..\TextOutDC.h"
#include "..\DpiCookbook/VsUIDpiHelper.h"
#include "MenuXP\Draw.h"
#include "IdeSettings.h"
#include "MenuXP\Tools.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifndef TVS_NOHSCROLL
#define TVS_NOHSCROLL 0x8000	// IE 5.0 or higher required
#endif

#ifndef HDF_FIXEDWIDTH
#define HDF_FIXEDWIDTH 0x0100	// Vista and Version 6.00
#endif


const unsigned int WM_COLUMN_RESIZED = ::RegisterWindowMessage("WM_COLUMN_RESIZED");
const unsigned int WM_COLUMN_SHOWN = ::RegisterWindowMessage("WM_COLUMN_SHOWN");
const unsigned int WM_ITEM_JUST_INSERTED = ::RegisterWindowMessage("WM_ITEM_JUST_INSERTED");



BOOL AFXAPI AfxExtractSubStringW(CStringW& rString, const CStringW &rFullString, int iSubString, WCHAR chSep)
{
	LPCWSTR lpszFullString = rFullString.GetString();

	while (iSubString--)
	{
		lpszFullString = wcschr(lpszFullString, chSep);
		if (lpszFullString == NULL)
		{
			rString.Empty();        // return empty string as well
			return FALSE;
		}
		lpszFullString++;       // point past the separator
	}
	LPCWSTR lpchEnd = wcschr(lpszFullString, chSep);
	if (!lpchEnd && (lpszFullString == rFullString.GetString()))
	{
		rString = rFullString;
		return true;
	}
	int nLen = (lpchEnd == NULL) ?
		static_cast<int>(wcslen(lpszFullString)) : (int)(lpchEnd - lpszFullString);
	ASSERT(nLen >= 0);
	Checked::memcpy_s(rString.GetBufferSetLength(nLen), nLen*sizeof(WCHAR),
		lpszFullString, nLen*sizeof(WCHAR));
	rString.ReleaseBuffer();	// Need to call ReleaseBuffer 
	// after calling GetBufferSetLength
	return TRUE;
}

// IMPLEMENT_DYNAMIC(CColumnTreeWndTempl, CWnd)

#pragma warning(push)
#pragma warning(disable: 4191)
BEGIN_TEMPLATE_MESSAGE_MAP(CColumnTreeWndTempl, TREECTRL, CWnd)
	ON_WM_CREATE()
 	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_HSCROLL()
	ON_NOTIFY(HDN_ITEMCHANGED, HeaderID, OnHeaderItemChanged)
	ON_NOTIFY(HDN_DIVIDERDBLCLICK, HeaderID, OnHeaderDividerDblClick)
	ON_NOTIFY(NM_CUSTOMDRAW, TreeID, OnTreeCustomDraw)
	ON_NOTIFY(TVN_DELETEITEM, TreeID, OnDeleteItem)
	ON_NOTIFY(TVN_SELCHANGED, TreeID, OnSelectionChanged)
	ON_REGISTERED_MESSAGE(WM_ITEM_JUST_INSERTED, OnItemJustInserted)
	ON_WM_PARENTNOTIFY()
	ON_WM_TIMER()
	ON_WM_SETFOCUS()
END_MESSAGE_MAP()
#pragma warning(pop)



template<typename TREECTRL>
CColumnTreeWndTempl<TREECTRL>::CColumnTreeWndTempl()
{
	reposition_controls_pending = 0;
	mCountAtLastRepositionCheck = 0;
	last_find = NULL;
	markall = false;
	last_markall_matchcase = false;

	__if_exists(TREECTRL::outer_OnTreeCustomDraw)
	{
		m_Tree.outer_OnTreeCustomDraw = [this](NMHDR *pNMHDR, LRESULT *pResult) {
			return OnTreeCustomDraw(pNMHDR, pResult);
		};
	}
}

template<typename TREECTRL>
CColumnTreeWndTempl<TREECTRL>::~CColumnTreeWndTempl()
{
}


template<typename TREECTRL>
int CColumnTreeWndTempl<TREECTRL>::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	// create tree and header controls as children
	m_Tree.Create(WS_CHILD | WS_VISIBLE | TVS_NOHSCROLL | TVS_NOTOOLTIPS, CRect(), this, TreeID);
#ifdef ALLOW_MULTIPLE_COLUMNS
	const int header_style = WS_CHILD | WS_VISIBLE | HDS_FULLDRAG;
#else
	const int header_style = WS_CHILD | HDS_FULLDRAG;
#endif
	m_Header.Create(header_style, CRect(), this, HeaderID);

	// set correct font for the header
// 	CFont* pFont = m_Tree.GetFont();
// 	m_Header.SetFont(pFont);

	// check if the common controls library version 6.0 is available
	static bool once = true;
	static BOOL bIsComCtl6 = FALSE;

	if (once)
	{
		bool freeLib = false;
		HMODULE hComCtlDll = GetModuleHandleA("comctl32.dll");
		if (!hComCtlDll)
		{
			hComCtlDll = LoadLibraryA("comctl32.dll");
			if (hComCtlDll)
				freeLib = true;
		}
		if (hComCtlDll)
		{
			once = false;
			typedef HRESULT (CALLBACK *PFNDLLGETVERSION)(DLLVERSIONINFO*);

			PFNDLLGETVERSION pfnDllGetVersion = (PFNDLLGETVERSION)(uintptr_t)::GetProcAddress(hComCtlDll, "DllGetVersion");

			if (pfnDllGetVersion)
			{
				DLLVERSIONINFO dvi;
				ZeroMemory(&dvi, sizeof(dvi));
				dvi.cbSize = sizeof(dvi);

				HRESULT hRes = (*pfnDllGetVersion)(&dvi);

				if (SUCCEEDED(hRes) && dvi.dwMajorVersion >= 6)
					bIsComCtl6 = TRUE;
			}

			if (freeLib)
				FreeLibrary(hComCtlDll);
		}
	}

	// calculate correct header's height
// 	CDC* pDC = GetDC();
// 	pDC->SelectObject(pFont);
// 	CSize szExt = pDC->GetTextExtent("A");
// 	m_cyHeader = szExt.cy + (bIsComCtl6 ? 7 : 4);
// 	ReleaseDC(pDC);

	// offset from column start to text start
	m_xOffset = bIsComCtl6 ? 9 : 6;

	m_xPos = 0;
	UpdateColumns();

	SetTimer(1234, 100, NULL);

	return 0;
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnPaint()
{
	// do nothing
	CPaintDC dc(this);
}

template<typename TREECTRL>
BOOL CColumnTreeWndTempl<TREECTRL>::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);

//	UpdateScroller();
	RepositionControls();
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CRect rcClient;
	GetClientRect(&rcClient);
	int cx = rcClient.Width();

	int xLast = m_xPos;
	int m_cxTotal2 = m_cxTotal;
	if(m_cxTotal2 > cx)
		m_cxTotal2 += GetSystemMetricsForDpi(SM_CXVSCROLL);

	switch (nSBCode)
	{
	case SB_LINELEFT:
		m_xPos -= 15;
		break;
	case SB_LINERIGHT:
		m_xPos += 15;
		break;
	case SB_PAGELEFT:
		m_xPos -= cx;
		break;
	case SB_PAGERIGHT:
		m_xPos += cx;
		break;
	case SB_LEFT:
		m_xPos = 0;
		break;
	case SB_RIGHT:
		m_xPos = m_cxTotal2 - cx;
		break;
	case SB_THUMBTRACK:
		m_xPos = (int)nPos;
		break;
	}

	if (m_xPos < 0)
		m_xPos = 0;
	else if (m_xPos > m_cxTotal2 - cx)
		m_xPos = m_cxTotal2 - cx;

	if (xLast == m_xPos)
		return;

	SetScrollPos(SB_HORZ, m_xPos);
	RepositionControls(true);
}


template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnHeaderItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	static int cnt = 0;
	cnt++;

	if(cnt == 1 && !(::GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
		// workaround for headers bug (?)
		// if column is fixed width, but width is set to zero; Windows will allow to resize it
		for(int i = m_Header.GetItemCount() - 1; i > 0; i--) {
			if(!hidden_columns[(size_t)i])
				continue;
			HDITEM hdi;
			memset(&hdi, 0, sizeof(hdi));
			hdi.mask = HDI_WIDTH;
			m_Header.GetItem(i, &hdi);
			if(hdi.cxy == 0)
				continue;
			for(int j = i - 1; j >= 0; j--) {
				if(hidden_columns[(size_t)j])
					continue;
				m_arrColWidths[(size_t)j] += hdi.cxy;
				hdi.cxy = m_arrColWidths[(size_t)j];
				m_Header.SetItem(j, &hdi);
				hdi.cxy = 0;
				m_Header.SetItem(i, &hdi);
				break;
			}
			break;
		}
	}

	UpdateColumns();

	GetParent()->PostMessage(WM_COLUMN_RESIZED);

	m_Tree.Invalidate();
	cnt--;
}


template<typename TREECTRL>
TreeItemFlags CColumnTreeWndTempl<TREECTRL>::GetItemFlags(HTREEITEM hItem) const
{
	auto info = item_info_cache.find(hItem);
	if (info != item_info_cache.end())
	{
		return std::get<IICI_Flags>(info->second);
	}
	return TIF_None;
}


template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnHeaderDividerDblClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMHEADER* pNMHeader = (NMHEADER*)pNMHDR;

	AdjustColumnWidth(pNMHeader->iItem, TRUE);
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	auto dpiScope = SetDefaultDpiHelper();

	NMCUSTOMDRAW* pNMCustomDraw = (NMCUSTOMDRAW*)pNMHDR;
	NMTVCUSTOMDRAW* pNMTVCustomDraw = (NMTVCUSTOMDRAW*)pNMHDR;
	const bool kHasFocus = CWnd::GetFocus()->GetSafeHwnd() == m_Tree.GetSafeHwnd();
	const bool vs11theme = 
		gShellAttr->IsCppBuilder() ||
		(gShellAttr->IsDevenv11OrHigher() && CVS2010Colours::IsVS2010FindRefColouringActive());

	switch (pNMCustomDraw->dwDrawStage)
	{
	case CDDS_PREPAINT:
		if (vs11theme)
			TreeVS2010CustomDraw(m_Tree, this, pNMHDR, pResult, background_gradient_cache); // this will draw background
		else
			*pResult = CDRF_NOTIFYITEMDRAW;
		break;

	case CDDS_ITEMPREPAINT:
		if (vs11theme)
		{
			*pResult = CDRF_SKIPDEFAULT;		// TreeVS2010CustomDraw will set this, but, just in case
			goto do_item_paint;
		}
		else
			*pResult = CDRF_DODEFAULT | CDRF_NOTIFYPOSTPAINT;
		break;

	case CDDS_ITEMPOSTPAINT:
	do_item_paint:
		{
			HTREEITEM hItem = (HTREEITEM)pNMCustomDraw->dwItemSpec;
			CRect rcItem = pNMCustomDraw->rc;

			if (rcItem.IsRectEmpty())
			{
				// nothing to paint
				if (!vs11theme)
					*pResult = CDRF_DODEFAULT;
				break;
			}

			HTHEME theme = NULL;
			if (!vs11theme && m_Tree.vs2010_active && m_Tree.AreThemesAvailable())
				theme = ::OpenThemeData(m_Tree.m_hWnd, L"TREEVIEW");

			TextOutDc dc;
			dc.Attach(pNMCustomDraw->hdc);

			CRect rcLabel;
			m_Tree.GetItemRect(hItem, &rcLabel, TRUE);
			CRect rcFullItem;
			m_Tree.GetItemRect(hItem, &rcFullItem, false);

			COLORREF crTextBk = pNMTVCustomDraw->clrTextBk;
			COLORREF crText = pNMTVCustomDraw->clrText;
			COLORREF crWnd = GetSysColor(COLOR_WINDOW);
			const bool one_cell_row = !!(GetItemFlags(hItem) & TIF_ONE_CELL_ROW);

			// clear the original label rectangle
			CRect rcClear = rcLabel;

			if(!one_cell_row && m_arrColWidths.size())
			{
				if(rcClear.left > m_arrColWidths[0] - 1)
					rcClear.left = m_arrColWidths[0] - 1;
			}
			else
			{
				if(rcClear.left > GetAllColumnsWidth() - 1)
					rcClear.left = GetAllColumnsWidth() - 1;
			}
			if (theme || vs11theme)
				rcClear.right = rcFullItem.right;
			if (!vs11theme)				// in VS2010, this is handled in CDDS_PREPAINT
				dc.FillSolidRect(&rcClear, crWnd);		// gmit: clear also if themes are active!

			int nColsCnt = m_Header.GetItemCount();

#ifdef ALLOW_MULTIPLE_COLUMNS
			// draw vertical grid lines...
			const bool draw_grid_lines = true;
			const bool use_system_colours = false;
			int xOffset = 0;
			{
				CSaveDC savedc(dc);
				CPen pen;
				COLORREF grid = ::GetSysColor(COLOR_3DLIGHT);
				COLORREF grid2 = RGB((GetRValue(crWnd) + GetRValue(grid)) / 2, (GetGValue(crWnd) + GetGValue(grid)) / 2, (GetBValue(crWnd) + GetBValue(grid)) / 2);
				if(draw_grid_lines && !use_system_colours) {
					pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), grid2);
					dc.SelectObject(pen);
				}

				for (int i=0; i<nColsCnt; i++)
				{
					if(hidden_columns[i])
						continue;
					xOffset += m_arrColWidths[i];
					rcItem.right = xOffset-1;
					if(draw_grid_lines && (!one_cell_row || (i == (nColsCnt - 1)))) {
						if(use_system_colours) {
							dc.DrawEdge(&rcItem, BDR_SUNKENINNER, BF_RIGHT);
						} else {
							dc.MoveTo(rcItem.right - 1, rcItem.top);
							dc.LineTo(rcItem.right - 1, rcItem.bottom);
						}
					}
				}
				// ...and the horizontal ones
				if(draw_grid_lines) {
					if(use_system_colours) {
						dc.DrawEdge(&rcItem, BDR_SUNKENINNER, BF_BOTTOM);
					} else {
						dc.MoveTo(rcItem.left, rcItem.bottom - 1);
						dc.LineTo(rcItem.right, rcItem.bottom - 1);
					}
				}
			}
#endif

			bool process_markers = !!(GetItemFlags(hItem) & TIF_PROCESS_MARKERS);

		    CStringW strText = m_Tree.GetItemTextW(hItem);
			if(GetItemFlags(hItem) & TIF_DRAW_IN_BOLD) {
				process_markers = true;
				strText = MARKER_BOLD + strText + MARKER_NONE;
			}
			CStringW strSub;
			AfxExtractSubStringW(strSub, strText, 0, '\t');

			// calculate main label's size
			CRect rcText(0,0,0,0);
			if(process_markers) {
				MeasureText(dc, strSub, rcText);
			} else {
				dc.DrawTextW(strSub, &rcText, DT_NOPREFIX | DT_CALCRECT);
			}

//			rcLabel.right = min(rcLabel.left + rcText.right + 4, (one_cell_row ? GetAllColumnsWidth() : m_arrColWidths[0]) - 4);
			int textRight = rcLabel.left + rcText.right + 4;
			int colRight = ((!one_cell_row && m_arrColWidths.size()) ? m_arrColWidths[0] : GetAllColumnsWidth()) - 4;
			rcLabel.right = min(textRight, colRight);

			CRect rcBack = rcLabel;
			if (((GetWindowLong(m_Tree.m_hWnd, GWL_STYLE) & TVS_FULLROWSELECT) || one_cell_row) && !vs11theme)
			{
				rcBack.right = GetAllColumnsWidth() - 1;
				if (m_arrColWidths.size())
				{
					if (rcBack.left > m_arrColWidths[0] - 1)
						rcBack.left = m_arrColWidths[0] - 1;
				}
			}

			int theme_state = 0;
			if (rcBack.Width() < 0)
				crTextBk = crWnd;
			if (theme)
			{
				if(pNMCustomDraw->uItemState & CDIS_DISABLED)
					theme_state = 4/*TREIS_DISABLED*/;
				else if(pNMCustomDraw->uItemState & CDIS_HOT)
				{
					if(pNMCustomDraw->uItemState & CDIS_SELECTED)
						theme_state = 6/*TREIS_HOTSELECTED*/;
					else
					{
						if (vs11theme)
							theme_state = 1; // no hot in dev11
						else
							theme_state = 2/*TREIS_HOT*/;
					}
				}
				else if(pNMCustomDraw->uItemState & CDIS_SELECTED)
				{
					if(pNMCustomDraw->uItemState & CDIS_FOCUS)
						theme_state = 3/*TREIS_SELECTED*/;
					else
						theme_state = 5/*TREIS_SELECTEDNOTFOCUS*/;
				}
				else
					theme_state = 1/*TREIS_NORMAL*/;

				// gmit: drawing with TREIS_NORMAL will give us some kind of border; I have no idea why, but, we can skip it because it's white anyway
				if(theme_state != 1/*TREIS_NORMAL*/)
				{
					if(!(m_Tree.GetStyle() & TVS_FULLROWSELECT))
						rcFullItem.right = rcLabel.right + 4;

					// gmit: WARNING: drawing transparent background breaks Vista!!
// 					if(m_Tree.__IsThemeBackgroundPartiallyTransparent(theme, TVP_TREEITEM, theme_state))
// 						m_Tree.__DrawThemeParentBackground(m_Tree.m_hWnd, dc.m_hDC, rcClear);
					::DrawThemeBackground(theme, dc.m_hDC, TVP_TREEITEM, theme_state, rcFullItem, rcClear);
				}
			}
			else if (vs11theme)
			{
			    TreeVS2010CustomDraw(m_Tree, this, pNMHDR, pResult, background_gradient_cache, CRect(rcBack.left, rcBack.top, rcBack.right, rcBack.bottom), false);
			}
			else if (crTextBk != crWnd)	// draw label's background
			{
				dc.FillSolidRect(&rcBack, crTextBk);
			}

			// draw focus rectangle if necessary
			if ((pNMCustomDraw->uItemState & CDIS_FOCUS) && !theme && !vs11theme)
				dc.DrawFocusRect(&rcBack);

			// draw main label
			rcText = rcLabel;
			rcText.DeflateRect(2, 1);
			if(theme)
				::GetThemeColor(theme, TVP_TREEITEM, theme_state, TMT_TEXTCOLOR, &crText);
			else if (vs11theme)
			{
				if (kHasFocus && (pNMCustomDraw->uItemState & CDIS_SELECTED) && !(g_IdeSettings && g_IdeSettings->IsBlueVSColorTheme15()))
					crText = CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHTTEXT);
				else
					crText = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE);
			}
			dc.SetTextColor(crText);

			bool do_colour = Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorViews;
			if (!theme)
			{
				// if themed draw is active, we want colours on selected item since it looks good!
				do_colour &= !(pNMCustomDraw->uItemState & CDIS_SELECTED);
			}
			do_colour &= !(GetItemFlags(hItem) & TIF_DONT_COLOUR);

			{
				CSaveDC saved(dc);
				dc.SetBkMode(TRANSPARENT);

				std::list<std::pair<uint, uint> > inverted_chars;
				std::list<std::pair<uint, uint> > rect_chars;
				std::vector<int> x_positions;
				CRect y_position;

				if(process_markers) {		// remember indice which characters to invert/round with rect (important because those markers can appear in the middle of word, so they could break colouring)
					bool in_invert = false;
					bool in_rect = false;
					for(int i = 0; i < strSub.GetLength();) {
						std::list<std::pair<uint, uint> > *chars;
						bool *in;
						switch(strSub[i]) {
						case MARKER_INVERT:
							chars = &inverted_chars;
							in = &in_invert;
							break;
						case MARKER_RECT:
							chars = &rect_chars;
							in = &in_rect;
							break;
						default:
							i++;
							continue;
						}
						if(!*in) {
							chars->push_back(std::make_pair((uint)i, 100000u));
						} else {
							if(chars->back().first != (uint)i) {
								chars->back().second = (uint)i;
							} else {
								chars->pop_back();
							}
						}
						*in = !*in;
						strSub.Delete(i);
					}

					if(inverted_chars.size() || rect_chars.size())
						MeasureText(dc, strSub, y_position, &x_positions);
				}

				CRect textpos = rcText;

				{
					struct draw_text
					{
						LPCWSTR str;
						uint len;
						bool isDim;
						bool isBold;
						COLORREF bg;
						COLORREF fg;
						RECT rect;

						void reset()
						{
							str = nullptr;
							len = 0;
							isDim = false;
							isBold = false;
							bg = CLR_INVALID;
							fg = CLR_INVALID;
							memset(&rect, 0, sizeof(rect));
						}

						draw_text() { reset(); }
					};

					std::vector<draw_text> texts;
					{
						draw_text dt;
						for(int i = 0, len = 0; i < strSub.GetLength(); i += len)
						{
							LPCWSTR ptr = (LPCWSTR)strSub + i;
							if(process_markers)
							{
								switch(strSub[i])
								{
								case MARKER_DIM:
									if(!(pNMCustomDraw->uItemState & CDIS_SELECTED))
										dt.isDim = true;
									break;
								case MARKER_BOLD:
									dt.isBold = true;
									break;
								case MARKER_REF:
									dt.isBold = true;
									if(do_colour)
									{
										dt.bg = Psettings->m_colors[C_Reference].c_bg;
										if(vs11theme && gColorSyncMgr)
										{
											const COLORREF refFg = Psettings->m_colors[C_Reference].c_fg & 0x00ffffff;
											if(refFg &&
											   refFg != crText &&
											   refFg != Psettings->m_colors[C_Text].c_fg &&
											   refFg != gColorSyncMgr->GetVsEditorTextFg())
											{
												// ref fg does not appear to be automatic, so explicitly set text color
												dt.fg = refFg;
											}
										}
									}
									break;
								case MARKER_ASSIGN:
									dt.isBold = true;
									if(do_colour)
									{
										dt.bg = Psettings->m_colors[C_ReferenceAssign].c_bg;
										if(vs11theme && gColorSyncMgr)
										{
											const COLORREF refFg = Psettings->m_colors[C_ReferenceAssign].c_fg & 0x00ffffff;
											if(refFg &&
											   refFg != crText &&
											   refFg != Psettings->m_colors[C_Text].c_fg &&
											   refFg != gColorSyncMgr->GetVsEditorTextFg())
											{
												// ref fg does not appear to be automatic, so explicitly set text color
												dt.fg = refFg;
											}
										}
									}
									break;
								case MARKER_NONE:
									dt.bg = CLR_INVALID;
									dt.fg = vs11theme ? crText : CLR_INVALID;
									dt.isBold = false;
									dt.isDim = false;
									break;
								default:
									i--;
									ptr--;
									break;
								}
								i++;
								ptr++;
								if(i >= strSub.GetLength())
									break;
								int e = strSub.Mid(i).FindOneOf(CStringW(MARKERS));
								len = (e != -1) ? e : (strSub.GetLength() - i);
							}
							else
							{
								len = strSub.GetLength();
							}
							if(len == 0)
								continue;

							dt.str = ptr;
							dt.len = (uint)len;

							texts.push_back(dt);

							dt.reset();
						}
					}

					// draw backgrounds

					for (draw_text & dt : texts)
					{
						saved.Reset(); // we need to reset font

						if (dt.isBold)
							::SelectObject(dc, ::GetBoldFont((HFONT)::GetCurrentObject(dc, OBJ_FONT)));

						CSize size;
						GetTextExtentExPointW(dc, dt.str, (int)dt.len, 0, nullptr, nullptr, &size);
						dt.rect = rcText;
						dt.rect.right = dt.rect.left + size.cx;
						rcText.left += size.cx;

						if (dt.bg != CLR_INVALID)
							dc.FillSolidRect(&dt.rect, dt.bg);
					}

					// draw texts

					VAColorPaintMessages pw(do_colour ? PaintType::View : PaintType::DontColor);

					UINT commonFlags = DT_NOPREFIX;
					if (wvWin8 == ::GetWinVersion())
						commonFlags |= DT_NOCLIP;

					for (draw_text & dt : texts)
					{
						saved.Reset(); // we need to reset font
						dc.SetBkMode(TRANSPARENT);

						if (dt.isBold)
							::SelectObject(dc, ::GetBoldFont((HFONT)::GetCurrentObject(dc, OBJ_FONT)));

						CDimDC dimDc(dc, dt.isDim);

						if (dt.fg != CLR_INVALID)
							dc.SetTextColor(dt.fg);

						dc.DrawTextW(CStringW(dt.str, (int)dt.len), &dt.rect, commonFlags | ((GetItemFlags(hItem) & TIF_END_ELLIPSIS) ? DT_END_ELLIPSIS : 0u) | ((GetItemFlags(hItem) & TIF_PATH_ELLIPSIS) ? DT_PATH_ELLIPSIS : 0u));
					}
				}

				// draw invert/rect markers
				if(x_positions.size()) {
					_ASSERTE(inverted_chars.size() || rect_chars.size());
					for(std::list<std::pair<uint, uint> >::const_iterator it = inverted_chars.begin(); it != inverted_chars.end(); ++it) {
						CRect rect(x_positions[it->first], y_position.top, (it->second != 100000) ? x_positions[it->second] : x_positions.back(), y_position.bottom);
						rect.OffsetRect(textpos.TopLeft());
						dc.SetBkMode(OPAQUE);
						dc.InvertRect(rect);
						if (vs11theme)
							dc.SetBkMode(TRANSPARENT);
					}
					for(std::list<std::pair<uint, uint> >::const_iterator it = rect_chars.begin(); it != rect_chars.end(); ++it) {
						CRect rect(x_positions[it->first], y_position.top, (it->second != 100000) ? x_positions[it->second] : x_positions.back(), y_position.bottom);
						rect.OffsetRect(textpos.TopLeft());
						dc.SetBkMode(OPAQUE);
						dc.InvertRect(rect);
						dc.SetBkMode(TRANSPARENT);
						CPen pen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), RGB(0, 0, 0));
						dc.SelectObject(&pen);
						dc.DrawFocusRect(rect);
					}
				}

				// restore dc
			}

			int xOffset2 = m_arrColWidths.size() ? m_arrColWidths[0] : 0;
			dc.SetBkMode(TRANSPARENT);

			if (!(GetWindowLong(m_Tree.m_hWnd, GWL_STYLE) & TVS_FULLROWSELECT))
			{
				if (vs11theme)
				{
					if (kHasFocus && (pNMCustomDraw->uItemState & CDIS_SELECTED) && !(g_IdeSettings && g_IdeSettings->IsBlueVSColorTheme15()))
						dc.SetTextColor(CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHTTEXT));
					else
						dc.SetTextColor(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE));
				}
				else
					dc.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
			}

			// draw other columns text
			if(!one_cell_row) 
			{
				for(size_t i=1; i<(size_t)nColsCnt; i++) 
				{
					if(hidden_columns[i])
						continue;
					if(AfxExtractSubStringW(strSub, strText, (int)i, '\t')) 
					{
						rcText = rcLabel;
						rcText.left = xOffset2;
						rcText.right = xOffset2 + m_arrColWidths[i];
						rcText.DeflateRect(m_xOffset, 1, 2, 1);
						dc.DrawTextW(strSub, &rcText, DT_NOPREFIX | DT_END_ELLIPSIS);
					}
					xOffset2 += m_arrColWidths[i];
				}
			}

			dc.Detach();

			if(theme)
				::CloseThemeData(theme);
		}
		if (!vs11theme)
			*pResult = CDRF_DODEFAULT;
		break;

	default:
		*pResult = CDRF_DODEFAULT;
	}
}


template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::UpdateColumns()
{
	m_cxTotal = 0;

	HDITEM hditem;
	hditem.mask = HDI_WIDTH;
	size_t nCnt = (size_t)m_Header.GetItemCount();
	m_arrColWidths.resize(nCnt);
	hidden_columns.resize(nCnt);

	// get column widths from the header control
	for(size_t i = 0; i < nCnt; i++) {
		if(hidden_columns[i])
			continue;
		if(m_Header.GetItem((int)i, &hditem)) {
			m_cxTotal += m_arrColWidths[i] = hditem.cxy;
			if(i == 0)
				m_Tree.m_cxFirstCol = hditem.cxy;
		}
	}
	m_Tree.m_cxTotal = m_cxTotal;

//	UpdateScroller();
	RepositionControls();
}

template<typename TREECTRL>
int CColumnTreeWndTempl<TREECTRL>::GetColumnWidth(int col) const {
	_ASSERTE((col >= 0) && (col < (int)m_arrColWidths.size()));
//	return hidden_columns[col] ? 0 : m_arrColWidths[col];
	return m_arrColWidths[(size_t)col];
}

template<typename TREECTRL>
bool CColumnTreeWndTempl<TREECTRL>::IsColumnShown(int col) const {
	_ASSERTE((col >= 0) && (col < (int)m_arrColWidths.size()));
	return !hidden_columns[(size_t)col];
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::UpdateScroller()
{
	CRect rcClient;
	GetClientRect(&rcClient);
	int cx = rcClient.Width();

//	int lx = m_xPos;

	int m_cxTotal2 = m_cxTotal;
	if(m_cxTotal2 > cx)
		m_cxTotal2 += GetSystemMetricsForDpi(SM_CXVSCROLL);

	if (m_xPos > m_cxTotal2 - cx)
		m_xPos = m_cxTotal2 - cx;
	if (m_xPos < 0)
		m_xPos = 0;

	SCROLLINFO scrinfo;
	scrinfo.cbSize = sizeof(scrinfo);
	scrinfo.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
	scrinfo.nPage = (UINT)cx;
	scrinfo.nMin = 0;
	scrinfo.nMax = m_cxTotal2;
	scrinfo.nPos = m_xPos;
	SetScrollInfo(SB_HORZ, &scrinfo);
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::RepositionControls(bool skip_column_resize)
{
	// reposition child controls
	if (m_Tree.m_hWnd)
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		int cx = rcClient.Width();
		int cy = rcClient.Height();

		// move to a negative offset if scrolled horizontally
		int x = 0;
		if (cx < m_cxTotal)
		{
			x = GetScrollPos(SB_HORZ);
			cx += x;
		}
#ifdef ALLOW_MULTIPLE_COLUMNS
		m_Header.MoveWindow(-x, 0, cx, m_cyHeader);
		m_Tree.MoveWindow(-x, m_cyHeader, cx, cy-m_cyHeader);
#else
		m_Tree.MoveWindow(-x, 0, cx, cy);

		if(m_arrColWidths.size() && !skip_column_resize) {
			static volatile LONG prevent_recursion = 0;				// RepositionControls() is called again when column width is altered
			if(!::InterlockedExchange(&prevent_recursion, 1)) {
//  uncomment if you'd like to have no horizontal scrollbar
// 				HDITEM hditem;
// 				hditem.mask = HDI_WIDTH;
// 				m_Header.GetItem(0, &hditem);
// 				hditem.cxy = cx - 4;
// 				m_arrColWidths[0] = hditem.cxy;
// 				m_Header.SetItem(0, &hditem);
				AdjustColumnWidth(0, true);
				InterlockedExchange(&prevent_recursion, 0);
			}
		}
#endif
	}

	UpdateScroller();
}

template<typename TREECTRL>
BOOL CColumnTreeWndTempl<TREECTRL>::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	if (CWnd::OnNotify(wParam, lParam, pResult))
		return TRUE;

	// route notifications from CTreeCtrl to the parent window
	if (wParam == TreeID)
	{
		NMHDR* pNMHDR = (NMHDR*)lParam;
		pNMHDR->idFrom = (UINT)GetDlgCtrlID();
		pNMHDR->hwndFrom = m_hWnd;
		*pResult = GetParent()->SendMessage(WM_NOTIFY, pNMHDR->idFrom, lParam);
	}
	return TRUE;
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::AdjustColumnWidth(int nColumn, BOOL bIgnoreCollapsed)
{
	_ASSERTE((nColumn >= 0) && ((size_t)nColumn < hidden_columns.size()));
	if((nColumn < 0) || ((size_t)nColumn >= hidden_columns.size()))
		return;

	for(; nColumn > 0; nColumn--) {
		if(!hidden_columns[(size_t)nColumn])
			break;
	}

	CWaitCursor curs;

	int nMaxWidth = 0;

	if (!bIgnoreCollapsed)
	{
		for (auto & kvp : item_info_cache)
		{
			const auto & texts = std::get<IICI_Columns>(kvp.second);
			if ((size_t)nColumn < texts.size() && texts[(size_t)nColumn].width > nMaxWidth)
				nMaxWidth = texts[(size_t)nColumn].width;
		}
	}
	else
	{
		for (auto & kvp : item_info_cache)
		{
			auto hParent = std::get<IICI_Parent>(kvp.second);
			if (hParent && m_Tree.GetItemState(hParent, TVIS_EXPANDED) & TVIS_EXPANDED)
			{
				const auto & texts = std::get<IICI_Columns>(kvp.second);

				if ((size_t)nColumn < (int)texts.size() && texts[(size_t)nColumn].width > nMaxWidth)
					nMaxWidth = texts[(size_t)nColumn].width;
			}
		}
	}

	HDITEM hditem;
	hditem.mask = HDI_WIDTH;
	m_Header.GetItem(nColumn, &hditem);
#ifdef ALLOW_MULTIPLE_COLUMNS
	hditem.cxy = nMaxWidth /*+ 20*/;
#else
	CRect rect;
	m_Tree.GetParent()->GetClientRect(rect);
	hditem.cxy = max(nMaxWidth/* + 20*/, rect.Width() - 4);
#endif
	m_arrColWidths[(size_t)nColumn] = hditem.cxy;
	m_Header.SetItem(nColumn, &hditem);

	GetParent()->PostMessage(WM_COLUMN_RESIZED);
}

template<typename TREECTRL>
int CColumnTreeWndTempl<TREECTRL>::CalcItemWidth(HTREEITEM hItem, TreeItemFlags flags, const CStringW& strSub, int nColumn, int depth /*= -1*/)
{
	int nMaxWidth = 0;

	std::optional<int> nMaxWidthCharsCached = chars_cache ? chars_cache->measure_text(flags, strSub) : std::nullopt;

	if(nMaxWidthCharsCached)
		nMaxWidth = *nMaxWidthCharsCached;
	else
	{
		HDC hDc = ::GetWindowDC(NULL);
		if (hDc)
		{
			CDC* pDc = CDC::FromHandle(hDc);
			if (pDc)
			{
				CDC dc;
				if (dc.CreateCompatibleDC(pDc))
				{
					CFont* pOldFont = dc.SelectObject(m_Tree.GetFont());

					// calculate text width
					if (flags & (TIF_PROCESS_MARKERS | TIF_DRAW_IN_BOLD))
					{
						CRect rect(0, 0, 0, 0);
						MeasureText(dc, (flags & TIF_DRAW_IN_BOLD) ? (MARKER_BOLD + strSub + MARKER_NONE) : strSub, rect);
						nMaxWidth = rect.Width();
					}
					else
					{
// 						nMaxWidth = dc.GetTextExtent(strSub).cx;
						CSize size;
						::GetTextExtentPoint32W(dc, strSub, strSub.GetLength(), &size);
						nMaxWidth = size.cx;
					}

					dc.SelectObject(pOldFont);
					dc.DeleteDC();
				}
			}

			::ReleaseDC(NULL, hDc);
		}
	}

	// add indent and image space if first column
	if (nColumn == 0)
	{
		if (depth < 0)
		{
			// depth is simply a count of parents the hItem has
			depth = 0;
			for (HTREEITEM parent = m_Tree.GetParentItem(hItem);
				parent;
				parent = m_Tree.GetParentItem(parent))
			{
				depth++;
			}
		}

		int nIndent = depth;

		if (GetWindowLong(m_Tree.m_hWnd, GWL_STYLE) & TVS_LINESATROOT)
			nIndent++;

// 		int nImage, nSelImage;
// 		m_Tree.GetItemImage(hItem, nImage, nSelImage);
// 		if (nImage >= 0)
		nIndent++;// there is always an image in findref

		nMaxWidth += nIndent * m_Tree.GetIndent();

		if(::GetWindowLong(m_Tree.m_hWnd, GWL_STYLE) & TVS_CHECKBOXES)
			nMaxWidth += m_Tree.GetItemHeight() - 6;

		nMaxWidth += 16;
	}

	return nMaxWidth;
}



template<typename TREECTRL>
bool CColumnTreeWndTempl<TREECTRL>::ValidateCache(HTREEITEM hItem /*= nullptr*/)
{
	// This lambda method only updates text and width of columns in the cache.
	// It does not check validity of other cache entry contents like parent, flags and depth.
	// Listed properties are considered valid and are used to calculate the column width. 
	auto update_entry = [&](auto & cached) -> bool
	{
		_ASSERTE(cached.first);

		if (!cached.first)
			return false;

		// flags are preserved 
		auto flags = std::get<IICI_Flags>(cached.second);

#ifdef ALLOW_MULTIPLE_COLUMNS
		int nCnt = 0;
		if (flags & TIF_ONE_CELL_ROW)
			nCnt = 1;
		else
			nCnt = m_Header.GetItemCount();
#else
		const int nCnt = 1;
#endif

		bool is_invalid = false;
		auto & columns = std::get<IICI_Columns>(cached.second);

		if (columns.size() != nCnt)
		{
			is_invalid = true;
			columns.clear();
		}

		CStringW strText = m_Tree.GetItemTextW(cached.first);
		CStringW strSub;

		for (int column = 0; column < nCnt; column++)
		{
			if (AfxExtractSubStringW(strSub, strText, column, '\t'))
			{
				if (is_invalid)
				{
					auto depth = std::get<IICI_Depth>(cached.second); // depth is preserved 
					int width = CalcItemWidth(cached.first, flags, strSub, column, depth);
					columns.emplace_back(strSub, width);
				}
				else if (strSub != columns[(size_t)column].text)
				{
					is_invalid = true;
					auto depth = std::get<IICI_Depth>(cached.second); // depth is preserved 
					int width = CalcItemWidth(cached.first, flags, strSub, column, depth);
					auto & item = columns[(size_t)column];
					item.text = strSub;
					item.width = width;
				}
			}
		}

		return is_invalid;
	};

	bool has_changes = false;

	if (hItem)
	{
		// update specified item
		auto found = item_info_cache.find(hItem);
		if (found != item_info_cache.end())
		 	if (update_entry(*found))
				has_changes = true;
	}
	else
	{
		// update all items in cache
		for (auto & cached : item_info_cache)
			if (update_entry(cached))
				has_changes = true;
	}

	return has_changes;
}


template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::CacheItemInfo(HTREEITEM hItem, TreeItemFlags flags, bool forceUpdate /*= false*/, const HTREEITEM* hint_parent, const int* hint_depth, const CStringW* hint_text)
{
	_ASSERTE(hItem);

	if (!hItem)
		return;

	if (!forceUpdate)
	{
		auto it = item_info_cache.find(hItem);
		if (it != item_info_cache.end())
		{
			std::get<IICI_Flags>(it->second) = flags;
			return;
		}
	}

	HTREEITEM hParent = hint_parent ? ((*hint_parent != TVI_ROOT) ? *hint_parent : nullptr) : m_Tree.GetParentItem(hItem);
	assert(!hint_parent || ((*hint_parent != TVI_ROOT) ? *hint_parent : nullptr) == m_Tree.GetParentItem(hItem));

	int depth = 0;
#ifndef _DEBUG
	if(hint_depth)
		depth = *hint_depth;
	else
#endif
	{
		for (HTREEITEM parent = hParent; parent; parent = m_Tree.GetParentItem(parent))
			depth++;
		assert(!hint_depth || (*hint_depth == depth));
	}

	column_list texts;

	CStringW strText = hint_text ? *hint_text : m_Tree.GetItemTextW(hItem);
	assert(!hint_text || (*hint_text == m_Tree.GetItemTextW(hItem)));
	CStringW strSub;

#ifdef ALLOW_MULTIPLE_COLUMNS
	int nCnt = 0;
	if (GetItemFlags(hItem) & TIF_ONE_CELL_ROW)
		nCnt = 1;
	else
		nCnt = m_Header.GetItemCount();
#else
	const int nCnt = 1;
#endif

	for (int column = 0; column < nCnt; column++)
	{
		if (AfxExtractSubStringW(strSub, strText, column, '\t'))
		{
			int width = CalcItemWidth(hItem, flags, strSub, column, depth);
			texts.emplace_back(strSub, width);
		}
	}

	// this is the only place where all of data is placed at once
	item_info_cache[hItem] = std::make_tuple(hParent, flags, depth, std::move(texts));
}

template<typename TREECTRL>
int CColumnTreeWndTempl<TREECTRL>::GetAllColumnsWidth() const {
//	return std::accumulate(m_arrColWidths.begin(), m_arrColWidths.end(), 0);
	int ret = 0;
	for(size_t i = 0; i < m_arrColWidths.size(); i++) {
		if(hidden_columns[i])
			continue;
		ret += m_arrColWidths[i];
	}
	return ret;
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnDeleteItem(NMHDR *nmhdr, LRESULT *result) {
	NMTREEVIEW *nmtv = (NMTREEVIEW *)nmhdr;

	item_info_cache.erase(nmtv->itemOld.hItem);

	if(last_find == nmtv->itemOld.hItem)
		last_find = NULL;

	if(result)
		*result = 0;
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnSelectionChanged(NMHDR *nmhdr, LRESULT *result) {
	NMTREEVIEW *nmtv = (NMTREEVIEW *)nmhdr;

	HTREEITEM item = nmtv->itemNew.hItem;
	if(last_find != item) {
		UnmarkAll(unmark_rectangle);
		last_find = item;
		if(item && !IsFilenameItem(last_find))
			m_Tree_SetItemTextW(item, CStringW(MARKER_RECT_STR) + (CStringW(MARKER_RECT_STR) + m_Tree.GetItemTextW(item) + CStringW(MARKER_RECT_STR)) + CStringW(MARKER_RECT_STR));
	}

	if(result)
		*result = 0;
}

template<typename TREECTRL>
LRESULT CColumnTreeWndTempl<TREECTRL>::OnItemJustInserted(WPARAM wparam, LPARAM lparam) {
	if(!markall || !last_markall_rtext.GetLength())
		return 0;

	if(&m_Tree != (CTreeCtrl *)wparam)
		return 0;
	HTREEITEM item = (HTREEITEM)lparam;
	if(!item)
		return 0;

	MarkOne(item, last_markall_rtext, last_markall_matchcase);
	CacheItemInfo(item, TIF_None);

	return 0;
}


template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnDpiChanged(DpiChange change, bool& handled)
{
	if (chars_cache)
		chars_cache->flush();

	double scale = GetDpiChangeScaleFactor();

	if (change == DpiChange::AfterParent && !item_info_cache.empty())
	{
		for (auto & kvp : item_info_cache)
		{
			auto & texts = std::get<IICI_Columns>(kvp.second);
			for (size_t i = 0; i < texts.size(); i++)
			{
				texts[i].width = WindowScaler::Scale(texts[i].width, scale);
			}
		}
	}

	__super::OnDpiChanged(change, handled);

	if (change == CDpiAware::DpiChange::AfterParent)
	{
		RepositionControls();
	}
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnFontSettingsChanged(VaFontTypeFlags changed, bool & handled)
{
	__super::OnFontSettingsChanged(changed, handled);

	if (chars_cache)
		chars_cache->flush();

	if (changed == VaFontTypeFlags::VAFTF_EnvironmentFont)
	{
		// recalculate widths according to new font
		for (auto & kvp : item_info_cache)
		{
			auto & texts = std::get<IICI_Columns>(kvp.second);
			for (size_t i = 0; i < texts.size(); i++)
			{
				texts[i].width = CalcItemWidth(kvp.first, std::get<IICI_Flags>(kvp.second), texts[i].text, std::get<IICI_Depth>(kvp.second));
			}
		}

		RepositionControls();
	}
}


template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::SetItemFlags(HTREEITEM item, int add_flags, int remove_flags) {
	_ASSERTE(item);
	if(!item)
		return;

	CacheItemInfo(item, TreeItemFlags((GetItemFlags(item) & ~remove_flags) | add_flags));
}

template <typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::SetItemFlags2(HTREEITEM item, int add_flags, int remove_flags, const HTREEITEM* hint_parent, const int* hint_depth, const CStringW* hint_text)
{
	_ASSERTE(item);
	if (!item)
		return;

	CacheItemInfo(item, TreeItemFlags((GetItemFlags(item) & ~remove_flags) | add_flags), false, hint_parent, hint_depth, hint_text);
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::ShowColumn(int column, bool show) {
	_ASSERTE((column >= 0) && ((unsigned int)column < m_arrColWidths.size()));
	if((column < 0) || ((unsigned int)column >= m_arrColWidths.size()))
		return;
	if(column == 0)
		show = true;

	hidden_columns[(size_t)column] = !show;

	HDITEM item;
	memset(&item, 0, sizeof(item));
	item.mask = HDI_FORMAT | HDI_WIDTH;
	m_Header.GetItem(column, &item);
	if(show)
		item.fmt &= ~HDF_FIXEDWIDTH;
	else
		item.fmt |= HDF_FIXEDWIDTH;
	item.cxy = show ? m_arrColWidths[(size_t)column] : 0;
	m_Header.SetItem(column, &item);
}

#include "../StringUtils.h"

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnParentNotify(UINT message, LPARAM lParam) {
#ifdef ALLOW_MULTIPLE_COLUMNS
	if(message == WM_RBUTTONDOWN) {
		CPoint pt(::GetMessagePos());
		CRect rect;
		m_Header.GetWindowRect(rect);
		if(rect.PtInRect(pt)) {
//			CMenu menu;
//			menu.CreatePopupMenu();
			PopupMenuXP menu;
			for(int i = 0; i < m_Header.GetItemCount(); i++) {
				HDITEM item;
				memset(&item, 0, sizeof(item));
				item.mask = HDI_TEXT;
				const std::unique_ptr<char[]> textVec(new char[1024]);
				item.pszText = &textVec[0];
				item.cchTextMax = 1024;
				item.pszText[0] = 0;
				m_Header.GetItem(i, &item);
				menu.AppendMenu(MF_STRING | ((i == 0) ? (MF_GRAYED | MF_DISABLED) : 0) | (hidden_columns[i] ? 0 : MF_CHECKED), 1000 + i, item.pszText);
			}
			int id = menu.TrackPopupMenu(TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, this);
			id -= 1000;
			if((id > 0) && (id < m_Header.GetItemCount())) {
				ShowColumn(id, hidden_columns[id]);
				GetParent()->PostMessage(WM_COLUMN_SHOWN);
			}
		}
	}
#endif
}

/*void CColumnTreeWnd::MeasureText(CDC &dc, const CString &text, CRect &rect) {
	std::string normal;
	std::string bold;
	bool bold_state = false;
	for(int i = 0; i < text.GetLength(); i++) {
		switch(text[i]) {
		case MARKER_BOLD:
		case MARKER_REF:
		case MARKER_ASSIGN:
			bold_state = true;
			break;
		case MARKER_NONE:
			bold_state = false;
			break;
		default:
			(bold_state ? bold : normal).push_back(text[i]);
			break;
		}
	}

	dc.DrawText(normal.c_str(), rect, DT_NOPREFIX | DT_CALCRECT);

	HFONT font = (HFONT)::GetCurrentObject(dc, OBJ_FONT);
	::SelectObject(dc, ::GetBoldFont(font));
	CRect brect(0, 0, 0, 0);
	dc.DrawText(bold.c_str(), brect, DT_NOPREFIX | DT_CALCRECT);
	::SelectObject(dc, font);

	rect.bottom = max(rect.bottom, brect.bottom);
	rect.right += brect.right;
}*/


void CalcItemWidthCharsCache::flush()
{
	if (dc)
	{
		::DeleteDC(dc);
		dc = nullptr;
	}
	if (dc_bold)
	{
		::DeleteDC(dc_bold);
		dc_bold = nullptr;
	}

	std::fill(widths.begin(), widths.end(), (short)0);
	std::fill(bold_widths.begin(), bold_widths.end(), (short)0);
	widths_map.clear();
}

void CalcItemWidthCharsCache::start()
{
	flush();

	HDC wdc = ::GetWindowDC(nullptr);
	assert(wdc);
	if (!wdc)
		return;

	dc = ::CreateCompatibleDC(wdc);
	if (dc)
	{
		dc_bold = ::CreateCompatibleDC(wdc);
		if (!dc_bold)
		{
			::DeleteDC(dc);
			dc = nullptr;
		}
	}
	::DeleteDC(wdc);
	assert(dc && dc_bold);
	if (!dc || !dc_bold)
		return;

	HFONT font = (HFONT)::SendMessage(tree, WM_GETFONT, 0, 0);
	if (!font)
		font = (HFONT)::GetStockObject(SYSTEM_FONT);
	::SelectObject(dc, ::GetCachedFont(font));
	::SelectObject(dc_bold, ::GetBoldFont(font));
}

std::optional<int> CalcItemWidthCharsCache::measure_text(TreeItemFlags flags, const CStringW& strSub)
{
	if (!(dc && dc_bold))
		return std::nullopt;
	if (!(flags & (TIF_PROCESS_MARKERS | TIF_DRAW_IN_BOLD)))
		return std::nullopt;

	int width = 0;
	bool is_bold = false;
	for (wchar_t wc : std::wstring_view(strSub.GetString(), strSub.GetString() + strSub.GetLength()))
	{
		switch (wc)
		{
		case MARKER_BOLD:
		case MARKER_REF:
		case MARKER_ASSIGN:
			is_bold = !is_bold;
			break;
		case MARKER_DIM:
			break;
		case MARKER_NONE:
		case MARKER_RECT:
		case MARKER_INVERT:
			is_bold = false;
			break;
		default:
			uint16_t wc_u = (uint16_t)wc;
			short* wptr = (wc_u < 256) ? &(is_bold ? bold_widths : widths)[wc_u] : &widths_map[wc][is_bold ? 1u : 0u];
			if (!*wptr)
			{
				CRect char_rect(0, 0, 0, 0);
				::DrawTextW(is_bold ? dc_bold : dc, &wc, 1, char_rect, DT_NOPREFIX | DT_CALCRECT);
				*wptr = (short)char_rect.right;
			}
			width += *wptr;
			break;
		}
	}
	return width;
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::MeasureText(CDC &dc, const CStringW &text, CRect &rect, std::vector<int> *x_positions) {
	CSaveDC savedc(dc);
	HFONT font = (HFONT)::GetCurrentObject(dc, OBJ_FONT);
	rect.SetRectEmpty();
	if(x_positions)
		x_positions->push_back(0);

	bool isDim = false;
	HFONT boldFont = (HFONT)nullptr;

	for(int i = 0; i < text.GetLength(); i++) {
		switch(text[i]) {
		case MARKER_BOLD:
		case MARKER_REF:
		case MARKER_ASSIGN:
			if (!boldFont)
				boldFont = ::GetBoldFont(font);
			::SelectObject(dc, boldFont);
			if(x_positions)
				x_positions->push_back(x_positions->back());
			continue;
		case MARKER_DIM:
			isDim = true;
			if (x_positions)
				x_positions->push_back(x_positions->back());
			continue;
		case MARKER_NONE:
		case MARKER_RECT:
		case MARKER_INVERT:
			::SelectObject(dc, font);
			if(x_positions)
				x_positions->push_back(x_positions->back());
			continue;
		default:
			WCHAR str[2] = {text[i], '\0'};
			CRect char_rect(0, 0, 0, 0);
			CDimDC dimDc(dc, isDim);
			::DrawTextW(dc, str, 1, char_rect, DT_NOPREFIX | DT_CALCRECT);
			rect.bottom = max(rect.bottom, char_rect.bottom);
			rect.right += char_rect.right;
			if(x_positions)
				x_positions->push_back(rect.right);
			break;
		}
	}
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnTimer(UINT_PTR nIDEvent) {
	if(nIDEvent == 1234)
	{
		if(reposition_controls_pending)
		{
			if (reposition_controls_pending == mCountAtLastRepositionCheck || 3 >= reposition_controls_pending)
			{
				InterlockedExchange(&reposition_controls_pending, 0);
				mCountAtLastRepositionCheck = 0;
				RepositionControls();
			}
			else
				mCountAtLastRepositionCheck = reposition_controls_pending;
		}
		return;
	}

	CWnd::OnTimer(nIDEvent);
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::OnSetFocus(CWnd* pOldWnd)
{
	m_Tree.SetFocus();
}

inline int ReverseFind(const CStringW &text, const CStringW &find, int starting = 100000) {
	if(starting > text.GetLength())
		starting = text.GetLength();

	while(--starting >= 0) {
		int i;
		for(i = 0; i < find.GetLength(); i++) {
			if((starting - i) < 0)
				return -1;
			if(towlower(find[find.GetLength() - 1 - i]) != towlower(text[starting - i]))
				goto cont;
		}
		return starting - i + 1;
cont:	;
	}
	return -1;
}

//#define ONE_FIND_PER_LINE
template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::FindNext(CStringW text, bool match_case) {
	if(text.GetLength() == 0)
		return;

	int starting = 0;

	if(!last_find) {
		// start find from selected or first tree item
		last_find = m_Tree.GetSelectedItem();
		while(last_find && IsFilenameItem(last_find)) {
			last_find = m_Tree.GetChildItem(last_find);
		}
		if(!last_find) {
			last_find = m_Tree.GetRootItem();
			if(!last_find) return;
			while(m_Tree.GetChildItem(last_find))
				last_find = m_Tree.GetChildItem(last_find);
		}
	} else {
		// resume find from last found occurence; first strip old markers
		{
			CStringW item = m_Tree.GetItemTextW(last_find);
#ifndef ONE_FIND_PER_LINE
			if((item.GetLength() >= 2) && (item[0] == MARKER_RECT) && (item[1] == MARKER_RECT)) {
				// a special case where we have to start find from the 'last_find' item
				item.Replace(CStringW(MARKER_RECT_STR), L"");
				m_Tree_SetItemTextW(last_find, item);
				starting = 0;
				goto find_text;
			} else {
				int i = item.ReverseFind(MARKER_RECT);
				if(i != -1) {
					item.Delete(i);
					int i2 = item.ReverseFind(MARKER_RECT);
					if(i2 != -1) {
						item.Delete(i2);
						i--;
					}
					m_Tree_SetItemTextW(last_find, item);
					starting = i;
					goto find_text;
				}
			}
#else
			static const char strMARKER_RECT[2] = {MARKER_RECT, 0};
			item.Replace(strMARKER_RECT, "");
			m_Tree_SetItemTextW(last_find, item);
#endif
		}

repeat_search:
		// get next tree item
		starting = 0;
		while(last_find) {
			if(IsFilenameItem(last_find)) {
				last_find = m_Tree.GetChildItem(last_find);
				if(!IsFilenameItem(last_find))
					break;
				continue;
			}

			{
				HTREEITEM next_sibling = m_Tree.GetNextSiblingItem(last_find);
				if(next_sibling)
				{
					if(!IsFilenameItem(next_sibling))
					{
						last_find = next_sibling;
						break;
					}
					else
						continue;
				}
			}

			for (;;) {
				last_find = m_Tree.GetParentItem(last_find);
				if(!last_find)
					break;
				HTREEITEM next_sibling = m_Tree.GetNextSiblingItem(last_find);
				if(next_sibling) {
					last_find = next_sibling;
					break;
				}
			}
		}
		if(!last_find) {
			::MessageBeep(MB_ICONEXCLAMATION);
			return;
		}
	}

find_text:
	// try to find string within the line
	CStringW item = m_Tree.GetItemTextW(last_find);
	CStringW item2 = item;
	if(!match_case) {
		item2.MakeLower();
		text.MakeLower();
	}
#ifndef ONE_FIND_PER_LINE
	int i = item2.Find(text, starting);
	if(i != -1) {
		item.Insert(i + text.GetLength(), MARKER_RECT);
		item.Insert(i, MARKER_RECT);
		m_Tree_SetItemTextW(last_find, item);
	} else {
		goto repeat_search;
	}
#else
	int i = 100000;
	while(true) {
		i = ReverseFind(item2, text, i);
		if(i == -1) break;
		item.Insert(i + text.GetLength(), MARKER_RECT);
		item.Insert(i, MARKER_RECT);
	}
	if(item2.GetLength() == item.GetLength())
		goto repeat_search;
	m_Tree_SetItemTextW(last_find, item);
	FindSelect(last_find);
#endif
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::FindPrev(CStringW text, bool match_case) {
	if(text.GetLength() == 0)
		return;

	int starting = 100000;

	if(!last_find) {
		// start find from selected or last tree item
		last_find = m_Tree.GetSelectedItem();
		while(last_find && IsFilenameItem(last_find)) {
			last_find = m_Tree.GetChildItem(last_find);
//			if(last_find)
//				while(m_Tree.GetNextSiblingItem(last_find))
//					last_find = m_Tree.GetNextSiblingItem(last_find);
		}
		if(!last_find) {
			last_find = m_Tree.GetRootItem();
			if(!last_find) return;
			for (;;) {
				while(m_Tree.GetNextSiblingItem(last_find))
					last_find = m_Tree.GetNextSiblingItem(last_find);
				if(!IsFilenameItem(last_find))
					break;
				last_find = m_Tree.GetChildItem(last_find);
			}
			if(!last_find) return;
		}
	} else {
		// resume find from last found occurence; first strip old markers
		{
			CStringW item = m_Tree.GetItemTextW(last_find);
#ifndef ONE_FIND_PER_LINE
			if((item.GetLength() >= 2) && (item[item.GetLength() - 1] == MARKER_RECT) && (item[item.GetLength() - 2] == MARKER_RECT)) {
				// a special case where we have to start find from the 'last_find' item
				item.Replace(CStringW(MARKER_RECT_STR), L"");
				m_Tree_SetItemTextW(last_find, item);
				starting = item.GetLength();
				goto find_text;
			} else {
				int i = item.Find(MARKER_RECT);
				if(i != -1) {
					item.Delete(i);
					int i2 = item.Find(MARKER_RECT);
					if(i2 != -1)
						item.Delete(i2);
					m_Tree_SetItemTextW(last_find, item);
					starting = i;
					goto find_text;
				}
			}
#else
			static const char strMARKER_RECT[2] = {MARKER_RECT, 0};
			item.Replace(strMARKER_RECT, "");
			m_Tree_SetItemTextW(last_find, item);
#endif
		}

repeat_search:
		// get previous tree item
		starting = 100000;
		while(last_find) {
			HTREEITEM prev_sibling = m_Tree.GetPrevSiblingItem(last_find);
repeat_prev_sibling:
			if(prev_sibling) {
				if(!IsFilenameItem(prev_sibling)) {
					last_find = prev_sibling;
					break;
				} else {
					prev_sibling = m_Tree.GetChildItem(prev_sibling);
					while(prev_sibling && m_Tree.GetNextSiblingItem(prev_sibling))
						prev_sibling = m_Tree.GetNextSiblingItem(prev_sibling);
					goto repeat_prev_sibling;
				}
			}

			last_find = m_Tree.GetParentItem(last_find);
		}
		if(!last_find) {
			::MessageBeep(MB_ICONEXCLAMATION);
			return;
		}
	}

find_text:
	// try to find string within the line
	CStringW item = m_Tree.GetItemTextW(last_find);
	CStringW item2 = item;
	if(!match_case) {
		item2.MakeLower();
		text.MakeLower();
	}
#ifndef ONE_FIND_PER_LINE
	int i = ReverseFind(item2, text, starting);
	if(i != -1) {
		item.Insert(i + text.GetLength(), MARKER_RECT);
		item.Insert(i, MARKER_RECT);
		m_Tree_SetItemTextW(last_find, item);
	} else {
		goto repeat_search;
	}
#else
	int i = 100000;
	while(true) {
		i = ReverseFind(item2, text, i);
		if(i == -1) break;
		item.Insert(i + text.GetLength(), MARKER_RECT);
		item.Insert(i, MARKER_RECT);
	}
	if(item2.GetLength() == item.GetLength())
		goto repeat_search;
	m_Tree_SetItemTextW(last_find, item);
	FindSelect(last_find);
#endif
}

#ifdef ONE_FIND_PER_LINE
template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::FindSelect(HTREEITEM item) {
	if(!item) return;

	m_Tree.SelectItem(item);

	NMHDR nmhdr;
	nmhdr.idFrom = TreeID;
	nmhdr.hwndFrom = m_Tree;
	nmhdr.code = NM_RETURN;
	SendMessage(WM_NOTIFY, nmhdr.idFrom, (LPARAM)&nmhdr);
}
#endif


int myCStringReplace(CStringW &str, const CStringW &find_what, bool case_sensitive = true, uint trim_on_find_left = 0, uint trim_on_find_right = 0, LPCWSTR new_prefix = NULL, LPCWSTR new_suffix = NULL) {
	if(!find_what.GetLength())
		return 0;
	if(!new_prefix)
		new_prefix = L"";
	if(!new_suffix)
		new_suffix = L"";

	std::list<std::pair<CStringW, bool> > decomposed;		// bool is true if substring contains match
	decomposed.push_back(std::make_pair("", false));

	// decompose string to matches/non-matches
	for(int i = 0; i < str.GetLength();) {
		if (!(case_sensitive ? wcsncmp : _wcsnicmp)((LPCWSTR)str + i, find_what, (size_t)find_what.GetLength()))
		{
			CStringW tmpStr = str.Mid(i, find_what.GetLength());
			decomposed.push_back(std::make_pair(tmpStr, true));
			decomposed.push_back(std::make_pair("", false));
			i += (int)find_what.GetLength();
		} else {
			decomposed.back().first.Append(str.Mid(i, 1));
			i++;
		}
	}

	// replace what's necessary
	int ret = 0;
	for(auto it = decomposed.begin(); it != decomposed.end(); ++it) {
		if(!it->second)
			continue;
		CStringW str2 = it->first;
		if ((uint) str2.GetLength() >= trim_on_find_left)
			str2.Delete(0, (int)trim_on_find_left);
		if ((uint) str2.GetLength() >= trim_on_find_right)
			str2.Delete(int(str2.GetLength() - trim_on_find_right), (int)trim_on_find_right);
		it->first = new_prefix + str2 + new_suffix;
		ret++;
	}

	// compose string again
	str = "";
	for(auto it = decomposed.begin(); it != decomposed.end(); ++it) {
		str.Append(it->first);
	}

	return ret;
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::MarkAll(const CStringW &rtext, bool match_case) {
	CLockWindow lw(*this);
	UnmarkAll();

	last_markall_rtext = rtext;
	last_markall_matchcase = match_case;

	if (!rtext.GetLength())
	{
		mMarksActive = false;
		return;
	}

	for(tree_iterator it = tree_iterator::begin(m_Tree); it != tree_iterator::end(m_Tree); ++it) {
		MarkOne(*it, rtext, match_case);
	}
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::MarkOne(HTREEITEM item, const CStringW &rtext, bool match_case) {
	if(!rtext.GetLength())
		return;
	if(IsFilenameItem(item))
		return;

	CStringW text = m_Tree.GetItemTextW(item);
	// put invert markers around matches
	if(myCStringReplace(text, rtext, match_case, 0, 0, CStringW(MARKER_INVERT_STR), CStringW(MARKER_INVERT_STR)) > 0) {
		// put rect markers closer to match
		myCStringReplace(text, CStringW(MARKER_RECT_STR) + (CStringW(MARKER_INVERT_STR) + rtext + CStringW(MARKER_INVERT_STR)) + CStringW(MARKER_RECT_STR), match_case, 2, 2, CStringW(MARKER_INVERT_STR) + CStringW(MARKER_RECT_STR), CStringW(MARKER_RECT_STR) + CStringW(MARKER_INVERT_STR));
		// delete duplicate invert markers
		myCStringReplace(text, CStringW(MARKER_INVERT_STR) + (CStringW(MARKER_INVERT_STR) + rtext + CStringW(MARKER_INVERT_STR)) + CStringW(MARKER_INVERT_STR), match_case, 1, 1);
		myCStringReplace(text, CStringW(MARKER_INVERT_STR) + (CStringW(MARKER_INVERT_STR) + (CStringW(MARKER_RECT_STR) + rtext + CStringW(MARKER_RECT_STR)) + CStringW(MARKER_INVERT_STR)) + CStringW(MARKER_INVERT_STR), match_case, 1, 1);
		m_Tree_SetItemTextW(item, text);
		mMarksActive = true;
	}
}

template<typename TREECTRL>
void CColumnTreeWndTempl<TREECTRL>::UnmarkAll(UnmarkType what) {
	if (!mMarksActive)
		return;

	CLockWindow lw(*this);

	for(tree_iterator it = tree_iterator::begin(m_Tree); it != tree_iterator::end(m_Tree); ++it) {
		CStringW text = m_Tree.GetItemTextW(*it);
		int changed = 0;
		if(what & unmark_markall)
			changed += text.Replace(CStringW(MARKER_INVERT_STR), L"");
		if(what & unmark_rectangle) {
			last_find = NULL;
			changed += text.Replace(CStringW(MARKER_RECT_STR), L"");
		}
		if(changed)
			m_Tree_SetItemTextW(*it, text);
	}

	if (unmark_all == what)
		mMarksActive = false;
}

template<typename TREECTRL>
bool CColumnTreeWndTempl<TREECTRL>::IsFilenameItem(HTREEITEM item) const {
	if(!item)
		return false;

//	return !m_Tree.GetParentItem(item);
	return !!m_Tree.ItemHasChildren(item);
}


template class CColumnTreeWndTempl<CColumnTreeCtrl>;
template class CColumnTreeWndTempl<CColumnTreeCtrl2>;
