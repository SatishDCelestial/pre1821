// FindUsageDlg.cpp : implementation file
//
#include "stdafxed.h"
#include "resource.h"
#include "edcnt.h"
#include "FindUsageDlg.h"
#include "edcnt.h"
#include "mparse.h"
#include "expansion.h"
#include "VaTimers.h"
#include "VaMessages.h"
#include "DevShellService.h"
#include "DevShellAttributes.h"
#include "VARefactor.h"
#include "FontSettings.h"
#include "VaService.h"
#include "Settings.h"
#include <Shlwapi.h>
#include "RedirectRegistryToVA.h"
#include "StringUtils.h"
#include "FindReferencesThread.h"
#include "FindReferencesResultsFrame.h"
#include "WindowUtils.h"
#include "FindTextDlg.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "vsshell100.h"
#include "KeyBindings.h"
#include "GetFileText.h"
#include "FILE.H"
#include "IdeSettings.h"
#include "ImageListManager.h"
#include "ColorSyncManager.h"
#include "MenuXP\Draw.h"
#include "TextOutDC.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "VAAutomation.h"
#include "PARSE.H"
#include "fdictionary.h"
#include "VAHashTable.h"

#define countof(x) (sizeof((x)) / sizeof((x)[0]))

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifdef RAD_STUDIO
#define CM_BASE 0xB000
#define CM_CONTROLCHANGE (CM_BASE + 54)
#endif

#define UPDATE_CHECKBOX_TIMER_ID (731)
const wchar_t* searched_project_node_text = L"Searched project, double click to search solution";

const uint WM_FR_HIGHLIGHT = ::RegisterWindowMessage("WM_FR_HIGHLIGHT");

#define ID_CMD_FIND_NEXT_IN_RESULTS 0x304
#define ID_CMD_FIND_PREV_IN_RESULTS 0x305
#define ID_CMD_TOGGLE_TOOLTIPS 0x306
#define ID_CMD_TOGGLE_LINENUMBERS 0x307
#define ID_CMD_TOGGLE_HIGHLIGHT 0x308
#define ID_CMD_CLEAR_ALL 0x309
#define ID_CMD_STOP 0x30a
#define ID_CMD_TOGGLE_PROJECT_NODES 0x30b
#define ID_CMD_COPY_ALL 0x30c
#define IDC_FIND_IN_STRINGS_AND_COMMENTS 0x30d
#define ID_CMD_TOGGLE_DIRTY_NAV 0x30e
#define ID_CMD_TOGGLE_INCLUDES 0x30f
#define ID_CMD_TOGGLE_UNKNOWNS 0x310
#define ID_CMD_TOGGLE_Definitions 0x311
#define ID_CMD_TOGGLE_DefinitionAssigns 0x312
#define ID_CMD_TOGGLE_References 0x313
#define ID_CMD_TOGGLE_ReferenceAssigns 0x314
#define ID_CMD_TOGGLE_ScopeReferences 0x315
#define ID_CMD_TOGGLE_JsSameNames 0x316
#define ID_CMD_TOGGLE_ReferenceAutoVars 0x317
#define ID_CMD_TOGGLE_Creations 0x318
#define IDC_CMD_EXPAND_ALL 0x319
#define IDC_CMD_COLLAPSE_ALL 0x31a
#define IDC_CMD_COLLAPSE_FILE_NODES 0x31b

static bool IsHighlightAllAllowed()
{
	if (!gShellAttr)
		return false;

	if (!Psettings->mUseMarkerApi)
		return Psettings && Psettings->m_ActiveSyntaxColoring;

	return true;
}

/////////////////////////////////////////////////////////////////////////////
// FindUsageDlg

// find references ctor - does not save position
FindUsageDlg::FindUsageDlg()
    : ReferencesWndBase("FindRefDlg", FindUsageDlg::IDD, NULL, Psettings->mIncludeProjectNodeInReferenceResults, fdAll,
                        flAntiFlicker | flNoSavePos),
      mInteractivelyHidden(false), mHasClonedResults(false), mFindCaseSensitive(false), mFindInReverseDirection(false),
      mMarkAll(m_treeSubClass.markall), mFileCount(0), mFileNodesRemoved(0), mRefNodesRemoved(0),
      mOkToUpdateCount(false)
#ifdef RAD_STUDIO	
	, mParent(nullptr)
#endif
{
	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		bool isConstructor = ed->GetSymDtype().type() == FUNC && ed->GetSymDtype().Attributes() & V_CONSTRUCTOR;
		bool isOpenParenAfterClass = ed->GetSymDtype().type() == CLASS && ed->IsOpenParenAfterCaretWord();

		if (isConstructor || isOpenParenAfterClass)
		{
			// setup filtering for constructor
			mDisplayTypeFilter = 0;
			mDisplayTypeFilter |= (1 << FREF_Creation);
			mDisplayTypeFilter |= (1 << FREF_Creation_Auto);
		}
	}

	if (g_currentEdCnt)
		m_symScope = g_currentEdCnt->GetSymScope();

	ReadLastSettingsFromRegistry();
	SetSharedFileBehavior(FindReferencesThread::sfPerProject);
}

FindUsageDlg::FindUsageDlg(const FindReferences& refsToCopy, bool isClone, DWORD filter /*= 0xffffffff*/)
    : ReferencesWndBase(refsToCopy, "FindRefDlg", FindUsageDlg::IDD, NULL,
                        Psettings->mIncludeProjectNodeInReferenceResults, fdAll, flAntiFlicker | flNoSavePos, filter),
      mInteractivelyHidden(false), mHasClonedResults(isClone), mFindCaseSensitive(false),
      mFindInReverseDirection(false), mMarkAll(m_treeSubClass.markall), mFileCount(0), mFileNodesRemoved(0),
      mRefNodesRemoved(0), mOkToUpdateCount(false)
#ifdef RAD_STUDIO		
	, mParent(nullptr)
#endif
{
	m_symScope = refsToCopy.GetFindScope();

	ReadLastSettingsFromRegistry();
	SetSharedFileBehavior(FindReferencesThread::sfPerProject);
}

void FindUsageDlg::DoDataExchange(CDataExchange* pDX)
{
	if (!mHasClonedResults)
		GetDlgItem(IDC_HIGHLIGHTAll)->EnableWindow(::IsHighlightAllAllowed());
	ReferencesWndBase::DoDataExchange(pDX);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(FindUsageDlg, ReferencesWndBase)
//{{AFX_MSG_MAP(FindUsageDlg)
ON_BN_CLICKED(IDC_HIGHLIGHTAll, OnToggleHighlight)
ON_BN_CLICKED(IDC_CLOSE, OnCloseButton)
ON_WM_DRAWITEM()
ON_BN_CLICKED(IDC_NEXT, GotoNextItem)
ON_BN_CLICKED(IDC_PREVIOUS, GotoPreviousItem)
ON_BN_DOUBLECLICKED(IDC_NEXT, GotoNextItem)
ON_BN_DOUBLECLICKED(IDC_PREVIOUS, GotoPreviousItem)
ON_COMMAND(ID_CMD_FIND_NEXT_IN_RESULTS, OnFindNextCmd)
ON_COMMAND(ID_CMD_FIND_PREV_IN_RESULTS, OnFindPrevCmd)
ON_COMMAND(ID_EDIT_CUT, OnCut)
ON_COMMAND(ID_EDIT_COPY, OnCopy)
ON_COMMAND(ID_EDIT_CLEAR, RemoveSelectedItem)
ON_COMMAND(ID_CMD_CLEAR_ALL, RemoveAllItems)
ON_COMMAND(ID_CMD_COPY_ALL, OnCopyAll)
ON_COMMAND(ID_CMD_TOGGLE_HIGHLIGHT, OnToggleHighlight)
ON_COMMAND(ID_CMD_TOGGLE_TOOLTIPS, OnToggleTooltips)
ON_COMMAND(ID_CMD_TOGGLE_PROJECT_NODES, OnToggleProjectNodes)
ON_COMMAND(ID_CMD_TOGGLE_DIRTY_NAV, OnToggleDirtyNav)
ON_COMMAND(ID_CMD_TOGGLE_LINENUMBERS, OnToggleLineNumbers)
ON_COMMAND(IDC_CLONERESULTS, OnCloneResults)
ON_COMMAND(IDC_FIND_INHERITED_REFERENCES, OnToggleFilterInherited)
ON_COMMAND(IDC_FIND_REFERENCES_ALL_PROJECTS, OnToggleAllProjects)
ON_COMMAND(IDC_FIND_IN_STRINGS_AND_COMMENTS, OnToggleFilterComments)
ON_COMMAND(ID_CMD_TOGGLE_UNKNOWNS, OnToggleFilterUnknowns)
ON_COMMAND(ID_CMD_TOGGLE_INCLUDES, OnToggleFilterIncludes)
ON_COMMAND(ID_CMD_TOGGLE_Definitions, OnToggleFilterDefinitions)
ON_COMMAND(ID_CMD_TOGGLE_DefinitionAssigns, OnToggleFilterDefinitionAssigns)
ON_COMMAND(ID_CMD_TOGGLE_References, OnToggleFilterReferences)
ON_COMMAND(ID_CMD_TOGGLE_ReferenceAssigns, OnToggleFilterReferenceAssigns)
ON_COMMAND(ID_CMD_TOGGLE_ScopeReferences, OnToggleFilterScopeReferences)
ON_COMMAND(ID_CMD_TOGGLE_JsSameNames, OnToggleFilterJsSameNames)
ON_COMMAND(ID_CMD_TOGGLE_ReferenceAutoVars, OnToggleFilterAutoVars)
ON_COMMAND(ID_CMD_TOGGLE_Creations, OnToggleFilterCreations)
ON_COMMAND(ID_CMD_STOP, OnCancel)
ON_COMMAND(IDC_REFRESH, OnRefresh)
ON_COMMAND(IDC_CMD_EXPAND_ALL, OnExpandAll)
ON_COMMAND(IDC_CMD_COLLAPSE_ALL, OnCollapseAll)
ON_COMMAND(IDC_CMD_COLLAPSE_FILE_NODES, OnCollapseFileNodes)
ON_COMMAND(IDC_FINDINRESULTS, OnFind)
//}}AFX_MSG_MAP
ON_REGISTERED_MESSAGE(WM_COLUMN_RESIZED, OnColumnResized)
ON_REGISTERED_MESSAGE(WM_COLUMN_SHOWN, OnColumnShown)
ON_REGISTERED_MESSAGE(WM_FR_HIGHLIGHT, OnEnableHighlight)
ON_WM_TIMER()
ON_NOTIFY(TVN_KEYDOWN, IDC_TREE1, OnTreeKeyDown)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// FindUsageDlg message handlers

BOOL FindUsageDlg::OnInitDialog()
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	HWND hTree = ::GetDlgItem(m_hWnd, IDC_TREE1);
	if (hTree)
	{
		CRect treeRc;
		::GetWindowRect(hTree, &treeRc);
		ScreenToClient(&treeRc);
		CRect clientRc;
		GetClientRect(&clientRc);
		if (treeRc.bottom < clientRc.bottom)
		{
			treeRc.bottom = clientRc.bottom;
			::SetWindowPos(hTree, NULL, 0, 0, treeRc.Width(), treeRc.Height(),
			               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
		}
	}

	ReferencesWndBase::OnInitDialog(true);
	mHonorsDisplayTypeFilter = true;
	m_tree.ModifyStyle(TVS_CHECKBOXES, 0);
	UpdateFonts(VAFTF_All);

	if (mHasClonedResults)
	{
		CWnd* tmp = GetDlgItem(IDC_HIGHLIGHTAll);
		tmp->DestroyWindow();
		CRect rc;
		tmp = GetDlgItem(IDC_CLONERESULTS);
		tmp->GetWindowRect(&rc);
		const int buttonWidth = rc.Width();
		GetWindowRect(&rc);
		int newRight = rc.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(5);

		tmp = GetDlgItem(IDC_STATUS);
		tmp->GetWindowRect(&rc);
		rc.right = newRight;
		rc.left -= buttonWidth;
		ScreenToClient(&rc);
		tmp->MoveWindow(rc);
		AddSzControl(IDC_STATUS, mdResize, mdNone);
	}
	else
	{
		AddSzControl(IDC_HIGHLIGHTAll, mdRepos, mdNone);
		CWnd* tmp = GetDlgItem(IDC_HIGHLIGHTAll);
		if (tmp && CVS2010Colours::IsVS2010FindRefColouringActive())
			tmp->ModifyStyle(0, BS_OWNERDRAW);
	}

	if (::GetDlgItem(m_hWnd, IDC_CLOSE))
	{
		if (gShellAttr->IsMsdev() && !mHasClonedResults)
		{
			// force close button to be rectangle for nicer X appearance
			CRect rect;
			::GetClientRect(::GetDlgItem(m_hWnd, IDC_CLOSE), rect);
			::SetWindowPos(::GetDlgItem(m_hWnd, IDC_CLOSE), HWND_TOP, 0, 0, rect.Width(), rect.Width(),
			               SWP_NOMOVE | SWP_NOACTIVATE);
			AddSzControl(IDC_CLOSE, mdRepos, mdNone);
		}
		else
		{
			GetDlgItem(IDC_CLOSE)->EnableWindow(false);
			GetDlgItem(IDC_CLOSE)->ShowWindow(SW_HIDE);
		}
	}

	mTooltips.Create(this, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP);
	mySetProp(mTooltips.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	PrepareImages();

	if (mRefs->Count() /* mHasClonedResults*/)
	{
		PopulateListFromRefs();
		_ASSERTE(!mRefs->m_doHighlight);
	}

	SetTimer(UPDATE_CHECKBOX_TIMER_ID, 100, NULL);

	UpdateFonts(VAFTF_All);

	CWnd* tmp = GetDlgItem(IDC_HIGHLIGHTAll);
	if (tmp)
		tmp->SetFont(&m_font);

	tmp = GetDlgItem(IDC_STATUS);
	if (tmp)
		tmp->SetFont(&m_font);

	tmp = GetDlgItem(IDCANCEL);
	if (tmp)
		tmp->SetFont(&m_font);

	m_tree.SetFocus();
	return FALSE;
}

static void AddHighlightBorder(HBITMAP bitmap)
{
	if (!bitmap)
		return;
	CDC dc;
	dc.CreateCompatibleDC(NULL);
	BITMAP bm;
	memset(&bm, 0, sizeof(bm));
	::GetObject(bitmap, sizeof(bm), &bm);
	if (!bm.bmWidth || !bm.bmHeight)
		return;
	dc.SelectObject(bitmap);
	COLORREF borderClr = g_IdeSettings->GetColor(VSCOLOR_HIGHLIGHT);
	CRect rc(0, 0, bm.bmWidth, bm.bmHeight);
	ThemeUtils::FrameRectDPI(dc, rc, borderClr);
}

void FindUsageDlg::SubstituteButton(int buttonIdx, int idRes, LPCSTR buttonTxt, int idImageRes, int idSelectedImageRes,
                                    int idFocusedImageRes, int idDisabledImageRes, bool isCheckbox /*= false*/)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	{
		CWnd* tmp = GetDlgItem(idRes);
		if (tmp)
			tmp->DestroyWindow();
	}

	// [case: 142243] - provide DPI aware layout for buttons
	const int btW = 22;
	const int btH = 19;
	const int space = 1;
	const POINT padding = {2, 3};

	int offset = buttonIdx * (btW + space);

	CRect rect(padding.x + offset, padding.y, padding.x + offset + btW, padding.y + btH);

	VsUI::DpiHelper::LogicalToDeviceUnits(&rect);

#ifndef _WIN64
	if (gShellAttr->IsDevenv11OrHigher())
	{
		// this is a bit fragile - dependent upon the v11 versions of the IDB_REFS*
		// image resource ids being paired up with the non-v11 versions in resource.h:
		// #define IDB_REFSFIND                    296
		// #define IDB_REFSFINDv11                 297
		++idImageRes;
		++idSelectedImageRes;
		_ASSERTE(!idFocusedImageRes);
		++idDisabledImageRes;
	}
#endif // !_WIN64

	_ASSERTE(buttonIdx < RefsButtonCount);
	mButtons[buttonIdx].Create(buttonTxt,
	                           /*WS_TABSTOP|*/ WS_CHILD | WS_VISIBLE | BS_BITMAP | BS_OWNERDRAW, rect, this,
	                           (UINT)idRes);
	mButtons[buttonIdx].EnableCheckboxBehavior(isCheckbox);

	if (CVS2010Colours::IsVS2010CommandBarColouringActive())
	{
		bool doManualTransparency = true;
		CBitmap normal, sel, foc, dis;
#ifdef _WIN64
		if (gShellAttr->IsDevenv17OrHigher())
		{
			CRect margin(2, 1, 2, 1);
			const COLORREF bgClr = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", false);
			gImgListMgr->GetMonikerImageFromResourceId(normal, idImageRes, bgClr, 0, false, false, true, &margin);
			gImgListMgr->GetMonikerImageFromResourceId(sel, idImageRes, bgClr, 0, false, false, true, &margin);
			gImgListMgr->GetMonikerImageFromResourceId(foc, idImageRes, bgClr, 0, false, false, true, &margin);
			gImgListMgr->GetMonikerImageFromResourceId(dis, idImageRes, bgClr, 0, true, false, true, &margin);
			doManualTransparency = false;
		}
		else
#endif
		if (gShellAttr->IsDevenv11OrHigher())
		{
			normal.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idImageRes), IMAGE_BITMAP,
			                                        0, 0, LR_CREATEDIBSECTION);
			sel.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idSelectedImageRes),
			                                     IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
			foc.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idFocusedImageRes),
			                                     IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
			dis.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idDisabledImageRes),
			                                     IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
		}
		else
		{
			normal.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idImageRes), IMAGE_BITMAP,
			                                        0, 0, LR_LOADMAP3DCOLORS);
			sel.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idSelectedImageRes),
			                                     IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS);
			foc.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idFocusedImageRes),
			                                     IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS);
			dis.m_hObject = (HBITMAP)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(idDisabledImageRes),
			                                     IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS);
		}

#ifndef _WIN64
		if (gShellAttr->IsDevenv11OrHigher() && g_IdeSettings)
		{
			const COLORREF bgClr = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", false);
			// stuff them into an img list
			CImageList il;
			if (!il.Create(21, 16, ILC_COLOR32, 0, 0))
				return;

			il.Add(&normal, RGB(0, 0, 0));
			il.Add(&sel, RGB(0, 0, 0));
			il.Add(&dis, RGB(0, 0, 0));

			gImgListMgr->ThemeImageList(il, bgClr);
			CDC* dc = GetWindowDC();
			if (dc)
			{
				// now pull them out
				normal.DeleteObject();
				ExtractImageFromAlphaList(il, 0, dc, bgClr, normal);

				sel.DeleteObject();
				ExtractImageFromAlphaList(il, 1, dc, bgClr, sel);

				dis.DeleteObject();
				ExtractImageFromAlphaList(il, 2, dc, bgClr, dis);

				doManualTransparency = false;
			}
		}

		if (doManualTransparency)
		{
			const COLORREF toolbarbg_cache = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_BEGIN);
			HGDIOBJ bitmaps[4] = {normal.m_hObject, sel.m_hObject, foc.m_hObject, dis.m_hObject};
			for (int i = 0; i < countof(bitmaps); i++)
				ChangeBitmapColour((HBITMAP)bitmaps[i], CLR_INVALID, toolbarbg_cache);
		}

		VsUI::DpiHelper::LogicalToDeviceUnits(normal);
		VsUI::DpiHelper::LogicalToDeviceUnits(sel);
		if (idFocusedImageRes)
			VsUI::DpiHelper::LogicalToDeviceUnits(foc);
		VsUI::DpiHelper::LogicalToDeviceUnits(dis);
#endif

		if (isCheckbox && sel.m_hObject)
			AddHighlightBorder((HBITMAP)sel.m_hObject);

		mButtons[buttonIdx].m_bitmap.m_hObject = normal.Detach();
		mButtons[buttonIdx].m_bitmapSel.m_hObject = sel.Detach();
		mButtons[buttonIdx].m_bitmapFocus.m_hObject = foc.Detach();
		mButtons[buttonIdx].m_bitmapDisabled.m_hObject = dis.Detach();
	}
	else
		mButtons[buttonIdx].LoadBitmap(idImageRes, idSelectedImageRes, idFocusedImageRes, idDisabledImageRes);

	if (::IsWindow(mTooltips.m_hWnd))
	{
		TOOLINFO ti;
		memset(&ti, 0, sizeof(ti));
		ti.cbSize = sizeof(ti);
		ti.hwnd = m_hWnd;
		ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		ti.lpszText = (LPSTR)buttonTxt;
		ti.uId = (UINT_PTR)mButtons[buttonIdx].m_hWnd;
		mTooltips.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	}
}

void FindUsageDlg::OnFindNext()
{
	Find(true);
}

void FindUsageDlg::OnFindPrev()
{
	Find(false);
}

void FindUsageDlg::Find(bool next)
{
	if (mFindWhat.IsEmpty())
	{
		OnFind();
		return;
	}

	mIgnoreItemSelect = true;
	if (mFindInReverseDirection ^ next)
		m_treeSubClass.FindNext(mFindWhat, mFindCaseSensitive);
	else
		m_treeSubClass.FindPrev(mFindWhat, mFindCaseSensitive);
	//	if(m_treeSubClass.GetLastFound())
	m_tree.SelectItem(m_treeSubClass.GetLastFound());
	mIgnoreItemSelect = false;
}

void FindUsageDlg::MarkAll()
{
	m_treeSubClass.MarkAll(mFindWhat, mFindCaseSensitive);
}

void FindUsageDlg::UnmarkAll(UnmarkType what)
{
	m_treeSubClass.UnmarkAll(what);
}

void FindUsageDlg::OnDoubleClickTree()
{
	HTREEITEM item = m_tree.GetSelectedItem();

	CStringW str = m_tree.GetItemTextW(item);
	if (str.Find(searched_project_node_text) != -1)
	{
		OnToggleAllProjects();
	}
	else
	{
		__super::OnDoubleClickTree();
	}
}

void FindUsageDlg::OnDrawItem(int id, LPDRAWITEMSTRUCT dis)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (IDC_HIGHLIGHTAll == id && CVS2010Colours::IsVS2010FindRefColouringActive())
	{
		TextOutDc dc;
		dc.Attach(dis->hDC);
		int savedc = dc.SaveDC();

		CRect boxRect(dis->rcItem);
		CSize checkBoxSize(0, 0);

		static CVAThemeHelper th;
		do
		{
			if (!CVS2010Colours::IsExtendedThemeActive() && th.AreThemesAvailable())
			{
				HTHEME theme = ::OpenThemeData(dis->hwndItem, L"BUTTON");
				if (theme)
				{
					::GetThemePartSize(theme, dis->hDC, BP_CHECKBOX, CBS_CHECKEDNORMAL, nullptr, TS_TRUE,
					                   &checkBoxSize);
					boxRect.right = boxRect.left + checkBoxSize.cx;
					::DrawThemeBackground(theme, dis->hDC, BP_CHECKBOX,
					                      mRefs->m_doHighlight ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL, &boxRect,
					                      nullptr);
					::CloseThemeData(theme);
					break;
				}
			}

			checkBoxSize.SetSize(VsUI::DpiHelper::LogicalToDeviceUnitsX(12),
			                     VsUI::DpiHelper::LogicalToDeviceUnitsY(12));
			boxRect.left++;
			boxRect.right = boxRect.left + checkBoxSize.cx;
			boxRect.top += VsUI::DpiHelper::LogicalToDeviceUnitsY(2);
			boxRect.bottom = boxRect.top + checkBoxSize.cy;

			if (CVS2010Colours::IsExtendedThemeActive())
			{
				//				COLORREF crBackImg = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_BEGIN);
				COLORREF crActive = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE);
				COLORREF crInactive = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_INACTIVE);
				COLORREF crBox = ThemeUtils::InterpolateColor(crInactive, crActive, 0.5);

				if (0 == crBox && crActive)
					crBox = crActive;
				else if (crBox > 0xffffff)
					crBox = crActive;

				boxRect.top += VsUI::DpiHelper::LogicalToDeviceUnitsY(2);
				boxRect.bottom += VsUI::DpiHelper::LogicalToDeviceUnitsY(2);

				if (mRefs->m_doHighlight)
					ThemeUtils::DrawSymbol(dc, boxRect, crBox);

				CPenDC pen(dc, crBox);
				CBrushDC brush(dc, CLR_NONE);
				dc.Rectangle(boxRect);
			}
			else
				dc.DrawFrameControl(&boxRect, DFC_BUTTON,
				                    DFCS_BUTTONCHECK | DFCS_FLAT | (mRefs->m_doHighlight ? DFCS_CHECKED : 0u));
		} while (false);
		CRect txtRect(dis->rcItem);
		if (g_FontSettings->GetDpiScaleY() <= 1.0)
			txtRect.top += VsUI::DpiHelper::LogicalToDeviceUnitsY(2);
		else if (g_FontSettings->GetDpiScaleY() > 1.0)
			txtRect.top -= VsUI::DpiHelper::LogicalToDeviceUnitsY(1);
		if (txtRect.bottom < (txtRect.top + checkBoxSize.cy))
			txtRect.bottom = txtRect.top + checkBoxSize.cy + (checkBoxSize.cy / 3);
		txtRect.left = boxRect.right + (int)(checkBoxSize.cx / VsUI::DpiHelper::LogicalToDeviceUnitsX(4));
		dc.SetTextColor(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE));
		const WTString txt(::GetWindowTextString(dis->hwndItem));

		dc.DrawTextW(txt.Wide(), txtRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

		dc.RestoreDC(savedc);
		dc.Detach();
		return;
	}

	__super::OnDrawItem(id, dis);
}

void FindUsageDlg::OnCloseButton()
{
	ASSERT(gShellAttr->IsMsdev());

	OnCancel(); // stop search
	if (GetDlgItem(IDCANCEL))
		GetDlgItem(IDCANCEL)->EnableWindow(false);
	OnCancel(); // hide
}

void FindUsageDlg::OnCancel()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
	{
		mFindRefsThread->Cancel();
	}
	else
	{
		OnSearchComplete(0, true);
		UpdateStatus(TRUE, 0);
	}

	// stop button will only stop search, not close the window, as in VS7/8
	if (gShellAttr->IsMsdev() && GetDlgItem(IDCANCEL) && GetDlgItem(IDCANCEL)->IsWindowEnabled())
		return;

	m_tree.PopTooltip();
	if (gShellAttr->RequiresFindResultsHack() && !mHasClonedResults)
	{
		Hide(false);
		if (gActiveFindRefsResultsFrame && gActiveFindRefsResultsFrame->m_hWnd &&
		    gActiveFindRefsResultsFrame->IsWindowVisible())
		{
			// set focus to output window - but not if it is going away
			gShellSvc->GetFindReferencesWindow();
		}
		else if (g_currentEdCnt)
		{
			// if no output wnd, set focus to edit wnd
			g_currentEdCnt->vSetFocus();
		}
	}
}

HTREEITEM
GetLastTreeItem(CTreeCtrl* tree)
{
	HTREEITEM item = tree->GetRootItem();

	HTREEITEM curItem = item;
	while (curItem)
	{
		item = curItem;
		curItem = tree->GetNextItem(item, TVGN_NEXT);
	}

	if (item && tree->ItemHasChildren(item))
	{
		curItem = tree->GetNextItem(item, TVGN_CHILD);
		while (curItem)
		{
			item = curItem;
			curItem = tree->GetNextSiblingItem(item);
		}
	}

	return item;
}

void FindUsageDlg::GotoNextItem()
{
	if (!gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsNext))
		return;

	if (mNavigationList.size() != m_tree.GetCount())
		BuildNavigationList(TVI_ROOT);

	HTREEITEM gotoItem = NULL;
	const HTREEITEM curItem = m_tree.GetSelectedItem();
	bool passedCurItem = curItem ? false : true;
	NavList::const_iterator it;
	bool makeParentVisible = curItem ? false : true;

	for (it = mNavigationList.begin(); it != mNavigationList.end(); ++it)
	{
		if (!passedCurItem)
		{
			if ((*it).mItem == curItem)
				passedCurItem = true;
			continue;
		}

		if (IS_FILE_REF_ITEM((*it).mRefData))
			continue;

		FindReference* ref = mRefs->GetReference((uint)(*it).mRefData);
		if (ref && ref->ShouldDisplay())
		{
			gotoItem = (*it).mItem;
			break;
		}
	}

	if (!gotoItem)
	{
		// wrap around
		_ASSERTE(it == mNavigationList.end());
		for (it = mNavigationList.begin(); it != mNavigationList.end(); ++it)
		{
			if (IS_FILE_REF_ITEM((*it).mRefData))
				continue;

			FindReference* ref = mRefs->GetReference((uint)(*it).mRefData);
			if (ref && ref->ShouldDisplay())
			{
				gotoItem = (*it).mItem;
				makeParentVisible = true;
				break;
			}
		}
	}

	if (makeParentVisible)
	{
		HTREEITEM root = m_tree.GetRootItem();
		// wrapped around - make parent visible (so that child isn't first visible item)
		if (root)
			m_tree.EnsureVisible(root);
	}

	SelectItem(gotoItem);
}

void FindUsageDlg::GotoPreviousItem()
{
	if (!gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsPrev))
		return;

	if (mNavigationList.size() != m_tree.GetCount())
		BuildNavigationList(TVI_ROOT);

	HTREEITEM gotoItem = NULL;
	const HTREEITEM curItem = m_tree.GetSelectedItem();
	bool passedCurItem = curItem ? false : true;
	NavList::const_reverse_iterator it;
	bool makeParentVisible = curItem ? false : true;
	HTREEITEM parentItem = NULL;

	for (it = mNavigationList.rbegin(); it != mNavigationList.rend(); ++it)
	{
		if (!passedCurItem)
		{
			if ((*it).mItem == curItem)
				passedCurItem = true;
			continue;
		}

		if (IS_FILE_REF_ITEM((*it).mRefData))
			continue;

		FindReference* ref = mRefs->GetReference((uint)(*it).mRefData);
		if (ref && ref->ShouldDisplay())
		{
			gotoItem = (*it).mItem;
			if (makeParentVisible)
				parentItem = m_tree.GetParentItem(gotoItem);
			else
			{
				if (m_tree.GetPrevSiblingItem(gotoItem))
				{
					HTREEITEM item = m_tree.GetPrevSiblingItem(gotoItem);
					if (!m_tree.GetPrevSiblingItem(item))
						parentItem = m_tree.GetParentItem(item);
				}
				else
				{
					HTREEITEM item = m_tree.GetParentItem(gotoItem);
					if (m_tree.GetNextItem(item, TVGN_PREVIOUS))
						parentItem = m_tree.GetNextItem(item, TVGN_PREVIOUS);
				}

				if (parentItem)
					makeParentVisible = true;
			}
			break;
		}
	}

	if (!gotoItem)
	{
		// wrap around
		_ASSERTE(it == mNavigationList.rend());
		for (it = mNavigationList.rbegin(); it != mNavigationList.rend(); ++it)
		{
			if (IS_FILE_REF_ITEM((*it).mRefData))
				continue;

			FindReference* ref = mRefs->GetReference((uint)(*it).mRefData);
			if (ref && ref->ShouldDisplay())
			{
				gotoItem = (*it).mItem;
				makeParentVisible = true;
				parentItem = m_tree.GetParentItem(gotoItem);
				break;
			}
		}
	}

	if (makeParentVisible && parentItem)
	{
		// make parent visible for some context - depending upon
		// number of children and size of window, selecting child
		// might make this selection moot...
		m_tree.EnsureVisible(parentItem);
	}

	SelectItem(gotoItem);
}

void FindUsageDlg::SelectItem(HTREEITEM item)
{
	if (!item)
		return;

	m_tree.Select(item, TVGN_CARET);
	GoToSelectedItem();
}

void FindUsageDlg::UpdateStatus(BOOL done, int fileCount)
{
	mFileCount = fileCount;
	WTString msg;
	if (!done)
		msg.WTFormat("Searching %s...", mRefs->GetScopeOfSearchStr().c_str());
	else if (mFindRefsThread && mFindRefsThread->IsStopped())
		msg = "Search canceled.";
	else if (mRefs->flags & FREF_Flg_FindErrors)
	{
		msg.WTFormat("Found %zu parsing errors.", mRefs->Count());
		// Save errors to a text file c:\VAParseErrors.log
		WTofstream ofs("c:\\VAParseErrors.log");
		if (ofs.good())
		{
			size_t count = mRefs->Count();
			for (size_t i = 0; i < count; i++)
			{
				FindReference* ref = mRefs->GetReference(i);
				if (ref)
				{
					ofs << WTString(ref->file).c_str();
					ofs << ":";
					ofs << (int)ref->lineNo;
					ofs << " ";
					ofs << ref->lnText.c_str();
					ofs << "\r\n";
				}
			}
		}
	}
	else
	{
		mOkToUpdateCount = true;
		msg = GetCountString();
		if (gTestLogger && gTestLogger->IsDialogLoggingEnabled()) // case 93368
		{
			gTestLogger->LogStr(msg);
		}
		if (!mHasClonedResults)
		{
			GetDlgItem(IDC_HIGHLIGHTAll)->ShowWindow(SW_NORMAL);
			if (::IsHighlightAllAllowed() && Psettings->mHighlightFindReferencesByDefault)
			{
				CheckDlgButton(IDC_HIGHLIGHTAll, BST_CHECKED);
				PostMessage(WM_FR_HIGHLIGHT);
			}
		}
	}

#if !defined(VA_CPPUNIT) && defined(_DEBUG)
	if (mStartTime && mEndTime)
		msg.WTAppendFormat(" (%zums)", (size_t)std::chrono::duration_cast<std::chrono::milliseconds>(*mEndTime - *mStartTime).count());
	extern std::atomic<int64_t> total_time_running_in_main_thread_ns;
	extern std::atomic<int64_t> total_items_running_in_main_thread;
	extern std::atomic<int64_t> trottling_cnt_main_thread;
	msg.WTAppendFormat(" RMT(%lld in %lldms; trottled %lld)", total_items_running_in_main_thread.exchange(0), total_time_running_in_main_thread_ns.exchange(0) / 1000000, trottling_cnt_main_thread.exchange(0));

	extern std::atomic_uint64_t rfmt_progress;
	extern std::atomic_uint64_t rfmt_status;
	extern std::atomic_uint64_t rfmt_flush;
	extern std::atomic_uint64_t removed_by_marker;
	msg.WTAppendFormat(" progress(%llu) status(%llu) flush(%llu) removed_by_marker(%llu)", rfmt_progress.exchange(0), rfmt_status.exchange(0), rfmt_flush.exchange(0), removed_by_marker.exchange(0));
#endif

	::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());

	if (done)
		vLog("%s", msg.c_str());
}

void FindUsageDlg::OnToggleHighlight()
{
	OnEnableHighlight(!mRefs->m_doHighlight);
}

void FindUsageDlg::OnEnableHighlight(bool enable)
{
	_ASSERTE(!mHasClonedResults);
	if (g_References != mRefs)
		g_References = mRefs;

	CWnd* tmp = GetDlgItem(IDC_HIGHLIGHTAll);
	if (tmp->GetSafeHwnd())
		tmp->Invalidate(TRUE);

	if (enable || mRefs->m_doHighlight != (BOOL)enable)
	{
		if (!enable)
			mRefs->StopReferenceHighlights();
		else
		{
			mRefs->m_doHighlight = TRUE;
			if (Psettings->mUseMarkerApi)
				mRefs->AddHighlightReferenceMarkers();
			else
				::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
		}
	}
}

LRESULT
FindUsageDlg::OnEnableHighlight(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	OnEnableHighlight(true);
	return 1;
}

void FindUsageDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == UPDATE_CHECKBOX_TIMER_ID)
	{
		if (!mHasClonedResults)
		{
			CButton* b = (CButton*)GetDlgItem(IDC_HIGHLIGHTAll);
			if (b)
			{
				if (!!b->GetCheck() != mRefs->m_doHighlight)
					b->SetCheck(mRefs->m_doHighlight);
				if (b->IsWindowEnabled() != (BOOL)::IsHighlightAllAllowed())
					b->EnableWindow(::IsHighlightAllAllowed());
			}
		}
	}

	ReferencesWndBase::OnTimer(nIDEvent);
}

void FindUsageDlg::Show(int showType)
{
	ShowWindow(showType);
	mInteractivelyHidden = false;
}

void FindUsageDlg::Hide(bool changeParent /*= false*/)
{
	ShowWindow(SW_HIDE);
	if (changeParent)
		SetParent(NULL);
	mInteractivelyHidden = true;
}

WTString GetSurroundingLineText(const CStringW& fileName, int centerLine, int contextLines = 4)
{
	WTString retval;
	const WTString ftxt(GetFileText(fileName));

	int previousPos = 0;
	int curLinePos = 0;
	int lineCnt = 0;

	while (lineCnt < centerLine + contextLines)
	{
		curLinePos = ftxt.Find("\n", previousPos);
		if (curLinePos++ == -1)
			break;
		++lineCnt;
		if (centerLine < contextLines || lineCnt >= (centerLine - contextLines))
			retval += ftxt.Mid(previousPos, curLinePos - previousPos);
		previousPos = curLinePos;
	}

	return retval;
}

void TrimIncompleteCstyleComments(WTString& text)
{
	int previousPos = 0;
	for (;;)
	{
		const int commentStartPos = text.Find("/*", previousPos);
		const int commentEndPos = text.Find("*/", previousPos);
		if (-1 == commentStartPos && -1 == commentEndPos)
			break;

		if (-1 == commentStartPos || (-1 != commentEndPos && commentEndPos < commentStartPos))
		{
			text = text.Mid(commentEndPos + 2); // remove end of comment at start of block
			_ASSERTE(previousPos == 0);
		}
		else if (-1 == commentEndPos || (-1 != commentStartPos && commentStartPos > commentEndPos))
		{
			text = text.Left(commentStartPos - 1); // remove start of comment at end of block
			break;
		}
		else if (commentStartPos < commentEndPos)
			previousPos = commentEndPos + 2;
		else
		{
			_ASSERTE(!"commentEndPos <= commentStartPos - not possible");
			break;
		}
	}
}

void HandleTooltipWhitespace(WTString& text)
{
	text.ReplaceAll("\r", "");
	while (text[0] == '\n')
		text = text.Mid(1);
	while (text.GetLength() > 1 && text[text.GetLength() - 1] == '\n')
		text = text.Left(text.GetLength() - 1);
	text.ReplaceAll("\t", "    ");
}

WTString FindUsageDlg::GetTooltipText(HTREEITEM item)
{
	WTString txt;
	try
	{
		if (!item)
			return txt;

		const uint refId = (uint)m_tree.GetItemData(item);
		if (IS_FILE_REF_ITEM(refId))
		{
			bool isProjectNode = false;
			int children = 0;
			HTREEITEM child = m_tree.GetChildItem(item);
			while (child)
			{
				children++;
				if (!isProjectNode && m_tree.ItemHasChildren(child))
					isProjectNode = true;
				child = m_tree.GetNextSiblingItem(child);
			}

			if (isProjectNode)
			{
				WTString projName;
				const FindReference* ref = mRefs->GetReference(refId & ~kFileRefBit);
				if (ref)
					projName = ref->mProject;
				txt.WTFormat("References found in %d file%s", children, (children != 1) ? "s" : "");
				if (projName.GetLength())
					txt += " from project\n" + projName;
			}
			else if (!(m_tree.GetItemState(item, TVIS_EXPANDED | TVIS_EXPANDPARTIAL) &
			           (TVIS_EXPANDED | TVIS_EXPANDPARTIAL)))
			{
				txt.WTFormat("%d reference%s found", children, (children != 1) ? "s" : "");

				const FindReference* ref = mRefs->GetReference(refId & ~kFileRefBit);
				if (ref)
				{
					txt.append(" in ");
					txt.append(WTString(::GetBaseName(ref->file)).c_str());
				}

				txt.append("...");
			}
		}
		else
		{
			const FindReference* ref(mRefs->GetReference(refId));
			if (ref)
			{
				WTString surroundingText(::GetSurroundingLineText(ref->file, (int)ref->lineNo));
				::TrimIncompleteCstyleComments(surroundingText);
				::HandleTooltipWhitespace(surroundingText);
				txt.WTFormat("%s%sFile: %s:%ld", surroundingText.c_str(),
				             surroundingText.GetLength() && surroundingText[surroundingText.GetLength() - 1] == '\n'
				                 ? "\n"
				                 : "\n\n",
				             WTString(ref->file).c_str(), ref->lineNo);
			}
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VAFU:");
	}

	return txt;
}

BOOL FindUsageDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	NMHDR* pNMHDR = (NMHDR*)lParam;

	if (pNMHDR->code == TVN_GETINFOTIPA || pNMHDR->code == TVN_GETINFOTIPW)
	{
		if (pNMHDR->code == TVN_GETINFOTIPA)
		{
			LPNMTVGETINFOTIP lpGetInfoTipw = (LPNMTVGETINFOTIP)lParam;
			const HTREEITEM i = lpGetInfoTipw->hItem;
			const WTString txt(GetTooltipText(i));

			for (int n = 0; n < lpGetInfoTipw->cchTextMax && txt[n]; n++)
			{
				lpGetInfoTipw->pszText[n] = txt[n];
				lpGetInfoTipw->pszText[n + 1] = '\0';
			}
			return TRUE;
		}
		if (pNMHDR->code == TVN_GETINFOTIPW)
		{
			LPNMTVGETINFOTIPW lpGetInfoTipw = (LPNMTVGETINFOTIPW)lParam;
			const HTREEITEM i = lpGetInfoTipw->hItem;
			const WTString txt(GetTooltipText(i));
			int len = MultiByteToWideChar(CP_UTF8, 0, txt.c_str(), txt.GetLength(), lpGetInfoTipw->pszText,
			                              lpGetInfoTipw->cchTextMax);
			lpGetInfoTipw->pszText[len] = L'\0';
			return TRUE;
		}
	}

	const BOOL retval = ReferencesWndBase::OnNotify(wParam, lParam, pResult);
	if (retval && pResult && *pResult == 0 && NM_CLICK == pNMHDR->code)
	{
		// [case: 6021] fix for handling click notification - the tree reflects
		// it back to itself which causes mfc to skip the parent notify
		struct AFX_NOTIFY
		{
			LRESULT* pResult;
			NMHDR* pNMHDR;
		};
		AFX_NOTIFY notify;
		notify.pResult = pResult;
		notify.pNMHDR = pNMHDR;
		HWND hWndCtrl = pNMHDR->hwndFrom;
		// get the child ID from the window itself
		UINT_PTR nID = ((UINT)(WORD)::GetDlgCtrlID(hWndCtrl));
		UINT nCode = pNMHDR->code;
		return OnCmdMsg((UINT)nID, MAKELONG(nCode, WM_NOTIFY), &notify, NULL);
	}

	return retval;
}

LRESULT
FindUsageDlg::OnColumnResized(WPARAM wparam, LPARAM lparam)
{
#ifdef ALLOW_MULTIPLE_COLUMNS
	CRedirectRegistryToVA rreg;

	for (int i = 0; i < __column_countof; i++)
	{
		if (!m_treeSubClass.IsColumnShown(i))
			continue;
		AfxGetApp()->WriteProfileInt(mSettingsCategory, format("column%ld_width", i).c_str(),
		                             m_treeSubClass.GetColumnWidth(i));
	}
#endif
	return 0;
}

LRESULT
FindUsageDlg::OnColumnShown(WPARAM wparam, LPARAM lparam)
{
#ifdef ALLOW_MULTIPLE_COLUMNS
	CRedirectRegistryToVA rreg;

	for (int i = 0; i < __column_countof; i++)
	{
		AfxGetApp()->WriteProfileInt(mSettingsCategory, format("column%ld_hidden", i).c_str(),
		                             m_treeSubClass.IsColumnShown(i) ? 0 : 1);
	}
#endif
	return 0;
}

void FindUsageDlg::OnFind()
{
	if (!gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsFind))
		return;

	FindTextDlg dlg(this);
	dlg.DoModal();
}

bool FindUsageDlg::HasFindText()
{
	return mFindWhat.GetLength() > 0;
}

void FindUsageDlg::RemoveSelectedItem()
{
	// don't allow delete of parent items if find refs is still running and the
	// parent has no next sibling since the parent may be required for new hits
	const bool kFindRefsRunning = 1 > gVaService->QueryStatus(IVaService::ct_refactor, icmdVaCmd_FindReferences);
	HTREEITEM selItem = m_tree.GetSelectedItem();
	if (!selItem)
		return;

	int refId = (int)m_tree.GetItemData(selItem);
	if (IS_FILE_REF_ITEM(refId))
	{
		// don't allow removal of parent nodes that may still be used
		if (kFindRefsRunning && !m_tree.GetNextSiblingItem(selItem))
			return;
	}
	else
	{
		HTREEITEM parentItem = m_tree.GetParentItem(selItem);
		_ASSERTE(parentItem);
		if (parentItem)
		{
			int childCnt = 0;
			HTREEITEM child = m_tree.GetChildItem(parentItem);
			for (; child && childCnt < 2; ++childCnt)
				child = m_tree.GetNextSiblingItem(child);

			_ASSERTE(childCnt);
			if (childCnt == 1 && (!kFindRefsRunning || m_tree.GetNextSiblingItem(parentItem)))
			{
				selItem = parentItem;
				refId = (int)m_tree.GetItemData(selItem);
			}
		}
	}

	if (IS_FILE_REF_ITEM(refId))
	{
		// selItem is a file or project node
		bool removedFile = false;
		HTREEITEM child = m_tree.GetChildItem(selItem);
		while (child)
		{
			HTREEITEM childDepth2 = m_tree.GetChildItem(child);
			if (childDepth2)
			{
				// file node
				while (childDepth2)
				{
					const int childrefId = (int)m_tree.GetItemData(childDepth2);
					mRefs->HideReference(childrefId);
					++mRefNodesRemoved;
					childDepth2 = m_tree.GetNextSiblingItem(childDepth2);
				}
				++mFileNodesRemoved;
				removedFile = true;
			}
			else
			{
				// reference node
				const int childrefId = (int)m_tree.GetItemData(child);
				mRefs->HideReference(childrefId);
				++mRefNodesRemoved;
			}

			child = m_tree.GetNextSiblingItem(child);
		}

		if (!removedFile)
		{
			// [case: 71206]
			++mFileNodesRemoved;
		}
	}
	else if (mRefs->HideReference(refId))
		++mRefNodesRemoved;

	HTREEITEM parentItem = m_tree.GetParentItem(selItem);
	m_tree.DeleteItem(selItem);

	// check to see if we left a lone project node
	if (parentItem && parentItem != TVI_ROOT && !m_tree.ItemHasChildren(parentItem))
	{
		if (!kFindRefsRunning || m_tree.GetNextSiblingItem(parentItem))
			m_tree.DeleteItem(parentItem);
	}

	if (mOkToUpdateCount)
	{
		const WTString msg(GetCountString());
		::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());
	}
}

void FindUsageDlg::OnCopyAll()
{
	OnCopy(TVI_ROOT);
}

void FindUsageDlg::OnCopy()
{
	HTREEITEM hItem = m_tree.GetSelectedItem();
	if (!hItem)
		hItem = TVI_ROOT;

	OnCopy(hItem);
}

CStringW FindUsageDlg::CopyText(HTREEITEM hItem, CStringW spacer)
{
	CStringW txt;
	if (!hItem)
		return txt;

	if (TVI_ROOT == hItem)
	{
		txt = L"Solution\r\n";
	}
	else
	{
		CStringW itemTxt(m_tree.GetItemText(hItem));
		if (gTestsActive && itemTxt.GetLength() > 1 && itemTxt[1] == L':' && -1 != itemTxt.FindOneOf(L"\\/") &&
		    GlobalProject)
		{
			CStringW slnPath(GlobalProject->SolutionFile());
			slnPath = ::Path(slnPath);
			slnPath.MakeLower();
			CStringW lowerItemTxt(itemTxt);
			lowerItemTxt.MakeLower();
			if (0 == lowerItemTxt.Find(slnPath))
				itemTxt = L"(path removed)" + itemTxt.Mid(slnPath.GetLength());
		}

		txt = spacer + itemTxt + L"\r\n";
	}

	HTREEITEM childItem = m_tree.GetChildItem(hItem);
	if (childItem)
	{
		spacer += L"\t";
		do
		{
			if (m_tree.ItemHasChildren(childItem))
			{
				txt += CopyText(childItem, spacer);
			}
			else
			{
				CStringW itemTxt(m_tree.GetItemText(childItem));
				if (gTestsActive && itemTxt.GetLength() > 1 && itemTxt[1] == L':' && -1 != itemTxt.FindOneOf(L"\\/") &&
				    GlobalProject)
				{
					CStringW slnPath(GlobalProject->SolutionFile());
					slnPath = ::Path(slnPath);
					slnPath.MakeLower();
					CStringW lowerItemTxt(itemTxt);
					lowerItemTxt.MakeLower();
					if (0 == lowerItemTxt.Find(slnPath))
						itemTxt = L"(path removed)" + itemTxt.Mid(slnPath.GetLength());
				}

				txt += (spacer + itemTxt + L"\r\n");
			}
			childItem = m_tree.GetNextSiblingItem(childItem);
		} while (childItem);
	}

	return txt;
}

void FindUsageDlg::OnCopy(HTREEITEM hItem)
{
	CStringW txt(CopyText(hItem, L""));

	// remove display markers - can't do it by char replacement since that will
	// insert null chars - need to replace 1 char string with an empty string
	txt.Replace(L"\1", L"");
	txt.Replace(L"\2", L"");
	txt.Replace(L"\3", L"");
	txt.Replace(L"\4", L"");
	txt.Replace(L"\5", L"");
	txt.Replace(L"\6", L"");

	::SaveToClipboard(m_hWnd, txt);
}

void FindUsageDlg::OnCut()
{
	OnCopy();
	RemoveSelectedItem();
}

void FindUsageDlg::OnPopulateContextMenu(CMenu& contextMenu)
{
	WTString txt;
	contextMenu.AppendMenu(gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsRefresh)
	                           ? 0u
	                           : MF_GRAYED | MF_DISABLED,
	                       MAKEWPARAM(IDC_REFRESH, ICONIDX_REPARSE), "&Refresh Results");
	if (!mHasClonedResults)
	{
		contextMenu.AppendMenu(gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsClone)
		                           ? 0u
		                           : MF_GRAYED | MF_DISABLED,
		                       IDC_CLONERESULTS, "Clone Res&ults to New Window");
	}
	if (gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsStop))
	{
		txt = "&Stop Find References\t" + GetBindingTip("Build.Cancel", NULL, FALSE);
		contextMenu.AppendMenu(0, ID_CMD_STOP, txt.c_str());
	}

	contextMenu.AppendMenu(MF_SEPARATOR);
	txt = "&Find in Results\t" + GetBindingTip("Edit.Find", NULL, FALSE);
	contextMenu.AppendMenu(
	    gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsFind) ? 0u : MF_GRAYED | MF_DISABLED,
	    IDC_FINDINRESULTS, txt.c_str());
	txt = "Find &Next in Results\t" + GetBindingTip("Edit.FindNext", NULL, FALSE);
	contextMenu.AppendMenu(gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsFindNext)
	                           ? 0u
	                           : MF_GRAYED | MF_DISABLED,
	                       ID_CMD_FIND_NEXT_IN_RESULTS, txt.c_str());
	txt = "Find &Prev in Results\t" + GetBindingTip("Edit.FindPrevious", NULL, FALSE);
	contextMenu.AppendMenu(gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsFindPrev)
	                           ? 0u
	                           : MF_GRAYED | MF_DISABLED,
	                       ID_CMD_FIND_PREV_IN_RESULTS, txt.c_str());

	contextMenu.AppendMenu(MF_SEPARATOR);

	HTREEITEM selItem = m_tree.GetSelectedItem();
	const DWORD kSelectionDependentItemState = selItem ? 0u : MF_GRAYED | MF_DISABLED;
	txt = "Cu&t\t" + GetBindingTip("Edit.Cut", NULL, FALSE);
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_CUT, ICONIDX_CUT), txt.c_str());
	txt = "&Copy\t" + GetBindingTip("Edit.Copy", NULL, FALSE);
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_COPY, ICONIDX_COPY), txt.c_str());
	contextMenu.AppendMenu(mReferencesCount - mRefNodesRemoved ? 0u : MF_GRAYED | MF_DISABLED,
	                       MAKEWPARAM(ID_CMD_COPY_ALL, ICONIDX_COPY), "Cop&y All");
	txt = "Delete\t" + GetBindingTip("Edit.Delete", NULL, FALSE);
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_CLEAR, ICONIDX_DELETE), txt.c_str());
	contextMenu.AppendMenu(mReferencesCount - mRefNodesRemoved ? 0u : MF_GRAYED | MF_DISABLED, ID_CMD_CLEAR_ALL,
	                       "Clear &All");

	mNumberOfTreeLevels = 0; // [case 142050]
	HTREEITEM treeItem = m_tree.GetChildItem(TVI_ROOT);
	if (treeItem)
	{
		mNumberOfTreeLevels = 1;
		treeItem = m_tree.GetChildItem(treeItem);
		if (treeItem)
		{
			mNumberOfTreeLevels = 2;
			treeItem = m_tree.GetChildItem(treeItem);
			if (treeItem)
				mNumberOfTreeLevels = 3;
		}
	}
	if (mNumberOfTreeLevels > 1)
	{
		contextMenu.AppendMenu(MF_SEPARATOR);
		contextMenu.AppendMenu(0, IDC_CMD_EXPAND_ALL, "E&xpand All");
		if (mNumberOfTreeLevels > 2)
			contextMenu.AppendMenu(0, IDC_CMD_COLLAPSE_ALL, "Collaps&e All");
		contextMenu.AppendMenu(0, IDC_CMD_COLLAPSE_FILE_NODES, "Collapse F&ile Nodes");
	}

	const bool threadActive = mFindRefsThread && mFindRefsThread->IsRunning();
	if (!threadActive)
	{
		int displaySubsection = 0;
		if (mHasOverrides || mRefTypes[FREF_Comment] || mRefTypes[FREF_IncludeDirective] || mRefTypes[FREF_Unknown])
		{
			displaySubsection = 1;
		}
		else
		{
			displaySubsection = (mRefTypes[FREF_Definition] || mRefTypes[FREF_DefinitionAssign] ? 1 : 0) +
			                    // 				(mRefTypes[FREF_DefinitionAssign] ? 1 : 0) +
			                    (mRefTypes[FREF_Reference] ? 1 : 0) + (mRefTypes[FREF_ReferenceAssign] ? 1 : 0) +
			                    (mRefTypes[FREF_ScopeReference] ? 1 : 0) + (mRefTypes[FREF_JsSameName] ? 1 : 0) +
			                    (mRefTypes[FREF_ReferenceAutoVar] ? 1 : 0) + (mRefTypes[FREF_Creation] ? 1 : 0) +
			                    (mRefTypes[FREF_Creation_Auto] ? 1 : 0);

			if (displaySubsection == 1)
			{
				// don't display toggle command if there is only a single type of reference
				displaySubsection = 0;
			}
		}

		if (displaySubsection)
		{
			contextMenu.AppendMenu(MF_SEPARATOR);
			if (mRefTypes[FREF_Definition] || mRefTypes[FREF_DefinitionAssign])
			{
				WTString label;
				if (IsCFile(gTypingDevLang))
				{
					if ((mRefTypes[FREF_Definition] + mRefTypes[FREF_DefinitionAssign]) > 1 || mHasOverrides)
						label = "Display &declarations and definitions\tD";
					else
						label = "Display &declaration\tD";
				}
				else
				{
					if ((mRefTypes[FREF_Definition] + mRefTypes[FREF_DefinitionAssign]) > 1 || mHasOverrides)
						label = "Display &definitions\tD";
					else
						label = "Display &definition\tD";
				}

				contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_Definition) ? MF_CHECKED : 0u,
				                       ID_CMD_TOGGLE_Definitions, label.c_str());
			}
			// don't differentiate between FREF_Definition and FREF_DefinitionAssign
			// (see also ReferencesWndBase::OnToggleFilterDefinitionAssigns and
			// ReferencesWndBase::OnToggleFilterDefinitions) 			if (mRefTypes[FREF_DefinitionAssign])
			// 				contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_DefinitionAssign) ? MF_CHECKED : 0,
			// ID_CMD_TOGGLE_DefinitionAssigns, "Display definition assigns");
			if (mRefTypes[FREF_Reference])
			{
				WTString label;
				const DType* data = mRefs->GetOrigDtype();
				const uint defType = data ? data->type() : UNDEF;
				if ((VAR == defType || PROPERTY == defType || LINQ_VAR == defType) || mRefTypes[FREF_ReferenceAssign])
					label = "Display &read references\tR";
				else
					label = "Display &references\tR";

				contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_Reference) ? MF_CHECKED : 0u,
				                       ID_CMD_TOGGLE_References, label.c_str());
			}
			if (mRefTypes[FREF_ReferenceAssign])
				contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_ReferenceAssign) ? MF_CHECKED : 0u,
				                       ID_CMD_TOGGLE_ReferenceAssigns, "Display &write references\tW");
			if (mRefTypes[FREF_ScopeReference])
				contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_ScopeReference) ? MF_CHECKED : 0u,
				                       ID_CMD_TOGGLE_ScopeReferences, "Display &scope references\tS");
			if (mRefTypes[FREF_JsSameName])
				contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_JsSameName) ? MF_CHECKED : 0u,
				                       ID_CMD_TOGGLE_JsSameNames, "Display &other references");
			if (mRefTypes[FREF_ReferenceAutoVar] || mRefTypes[FREF_Creation_Auto])
			{
				if (IsCFile(gTypingDevLang))
					contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_ReferenceAutoVar) ? MF_CHECKED : 0u,
					                       ID_CMD_TOGGLE_ReferenceAutoVars, "Display '&auto' references\tA");
				else
					contextMenu.AppendMenu(mDisplayTypeFilter & (1 << FREF_ReferenceAutoVar) ? MF_CHECKED : 0u,
					                       ID_CMD_TOGGLE_ReferenceAutoVars, "Display '&var' references\tV");
			}

			if (mHasOverrides)
				contextMenu.AppendMenu(Psettings->mDisplayWiderScopeReferences ? MF_CHECKED : 0u,
				                       IDC_FIND_INHERITED_REFERENCES,
				                       "Display &inherited and overridden references\tO");
			if (mRefTypes[FREF_Comment])
				contextMenu.AppendMenu(Psettings->mDisplayCommentAndStringReferences ? MF_CHECKED : 0u,
				                       IDC_FIND_IN_STRINGS_AND_COMMENTS, "Display co&mment and string hits\tM");
			if (mRefTypes[FREF_IncludeDirective])
				contextMenu.AppendMenu(Psettings->mFindRefsDisplayIncludes ? MF_CHECKED : 0u, ID_CMD_TOGGLE_INCLUDES,
				                       "Display #includ&e hits\tI");
			if (mRefTypes[FREF_Unknown])
				contextMenu.AppendMenu(Psettings->mFindRefsDisplayUnknown ? MF_CHECKED : 0u, ID_CMD_TOGGLE_UNKNOWNS,
				                       "Display un&known/guess hits\tG");
			if (mRefTypes[FREF_Creation] || mRefTypes[FREF_Creation_Auto])
				contextMenu.AppendMenu(
				    mDisplayTypeFilter & ((1 << FREF_Creation) | (1 << FREF_Creation_Auto)) ? MF_CHECKED : 0u,
				    ID_CMD_TOGGLE_Creations, "Display &creation/construction hits\tC");
		}
	}

	contextMenu.AppendMenu(MF_SEPARATOR);
	if (!mHasClonedResults)
	{
		UINT flags = ::IsHighlightAllAllowed() ? (mRefs->m_doHighlight ? MF_CHECKED : 0u) : (MF_GRAYED | MF_DISABLED);
		contextMenu.AppendMenu(flags, ID_CMD_TOGGLE_HIGHLIGHT, "&Highlight All\tH");
	}

	if (!threadActive)
		contextMenu.AppendMenu(Psettings->mDisplayReferencesFromAllProjects ? MF_CHECKED : 0u,
		                       IDC_FIND_REFERENCES_ALL_PROJECTS, "Search all pro&jects\tP");

	contextMenu.AppendMenu(Psettings->mUseTooltipsInFindReferencesResults ? MF_CHECKED : 0u, ID_CMD_TOGGLE_TOOLTIPS,
	                       "T&ooltips");
	if (!threadActive)
	{
		contextMenu.AppendMenu(Psettings->mLineNumbersInFindRefsResults ? MF_CHECKED : 0u, ID_CMD_TOGGLE_LINENUMBERS,
		                       "&Line Numbers (requires refresh)");
		contextMenu.AppendMenu(Psettings->mIncludeProjectNodeInReferenceResults ? MF_CHECKED : 0u,
		                       ID_CMD_TOGGLE_PROJECT_NODES, "&Show Projects");
	}
	contextMenu.AppendMenu(Psettings->mAlternateDirtyRefsNavBehavior ? MF_CHECKED : 0u, ID_CMD_TOGGLE_DIRTY_NAV,
	                       "Allo&w navigation to stale results");
}

void FindUsageDlg::OnToggleTooltips()
{
	if (!Psettings)
		return;

	Psettings->mUseTooltipsInFindReferencesResults = !Psettings->mUseTooltipsInFindReferencesResults;
	m_tree.EnableToolTips(Psettings->mUseTooltipsInFindReferencesResults);
}

void FindUsageDlg::OnToggleProjectNodes()
{
	if (!Psettings)
		return;
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return;

	Psettings->mIncludeProjectNodeInReferenceResults = !Psettings->mIncludeProjectNodeInReferenceResults;
	mDisplayProjectNodes = Psettings->mIncludeProjectNodeInReferenceResults;
	RemoveAllItems();
	PopulateListFromRefs();
}

void FindUsageDlg::OnToggleDirtyNav()
{
	if (!Psettings)
		return;

	Psettings->mAlternateDirtyRefsNavBehavior = !Psettings->mAlternateDirtyRefsNavBehavior;
}

void FindUsageDlg::OnToggleLineNumbers()
{
	if (!Psettings)
		return;
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return;

	Psettings->mLineNumbersInFindRefsResults = !Psettings->mLineNumbersInFindRefsResults;
	// run find refs again!  automatically call Refresh?
	// or modify FindReferences struct and vaparse, rebuild tree from data?
}

// [case: 2488] [find references results] add commands to Expand/Collapse All
void FindUsageDlg::ExpandOrCollapseNodes(HTREEITEM treeItem, UINT nCode, bool applyCollapseToProjectNodes)
{
	if (treeItem)
	{
		HTREEITEM childItem = m_tree.GetChildItem(treeItem);
		while (childItem)
		{
			if (m_tree.ItemHasChildren(childItem))
			{
				ExpandOrCollapseNodes(childItem, nCode, applyCollapseToProjectNodes);
				if (applyCollapseToProjectNodes || treeItem != TVI_ROOT || nCode != TVE_COLLAPSE ||
				    mNumberOfTreeLevels == 2) // optionally collapse project nodes
					m_tree.Expand(childItem, nCode);
			}
			childItem = m_tree.GetNextSiblingItem(childItem);
		}
	}
}

void FindUsageDlg::OnExpandAll()
{
	ExpandOrCollapseNodes(TVI_ROOT, TVE_EXPAND, true);
}

void FindUsageDlg::OnCollapseAll()
{
	ExpandOrCollapseNodes(TVI_ROOT, TVE_COLLAPSE, true);
}

void FindUsageDlg::OnCollapseFileNodes()
{
	ExpandOrCollapseNodes(TVI_ROOT, TVE_COLLAPSE, false);
}

void FindUsageDlg::OnCloneResults()
{
	if (!gVaService->QueryStatus(IVaService::ct_findRefResults, icmdVaCmd_RefResultsClone))
		return;

	if (gShellSvc)
		gShellSvc->CloneFindReferencesResults();
}

#ifdef REMOTEHEAP_TIMING_PROJLOAD_AND_FINDREF
std::unique_ptr<remote_heap::stats::event_timing> findref_timing;
#endif
void FindUsageDlg::OnSearchBegin()
{
#ifdef REMOTEHEAP_TIMING_PROJLOAD_AND_FINDREF
	findref_timing = std::make_unique<remote_heap::stats::event_timing>("Findref");
#endif

	mStartTime = std::chrono::steady_clock::now();
	mEndTime.reset();

	{
		CButton* tmp = ((CButton*)GetDlgItem(IDC_FIND_REFERENCES_ALL_PROJECTS));
		if (tmp)
			tmp->SetCheck(Psettings->mDisplayReferencesFromAllProjects ? BST_CHECKED : BST_UNCHECKED);
	}

	mFileCount = 0;
	mFileNodesRemoved = 0;
	mRefNodesRemoved = 0;
	mOkToUpdateCount = false;
	mNavigationList.clear();
	for (int idx = 0; idx < RefsButtonCount; ++idx)
	{
		if (mButtons[idx].m_hWnd)
			mButtons[idx].EnableWindow(false);
	}

	ReferencesWndBase::OnSearchBegin();
	CWnd* tmp = GetDlgItem(IDCANCEL);
	if (tmp)
	{
		tmp->EnableWindow(TRUE);
		tmp->ShowWindow(SW_NORMAL);
	}

	if (!mHasClonedResults)
	{
		tmp = GetDlgItem(IDC_HIGHLIGHTAll);
		if (tmp)
			tmp->ShowWindow(SW_HIDE);
	}
}

void FindUsageDlg::OnSearchComplete(int fileCount, bool wasCanceled)
{
#ifdef REMOTEHEAP_TIMING_PROJLOAD_AND_FINDREF
	findref_timing.reset();
#endif

	mEndTime = std::chrono::steady_clock::now();

	ReferencesWndBase::OnSearchComplete(fileCount, wasCanceled);
	CWnd* tmp = GetDlgItem(IDCANCEL);
	if (tmp)
	{
		tmp->EnableWindow(FALSE);
		tmp->ShowWindow(SW_HIDE);
	}

	if (!mHasClonedResults)
		GetDlgItem(IDC_HIGHLIGHTAll)->ShowWindow(SW_NORMAL);

	for (int idx = 0; idx < RefsButtonCount; ++idx)
	{
		if (mButtons[idx].m_hWnd)
			mButtons[idx].EnableWindow(true);
	}

	InspectReferences();

	if (mRefs->GetScopeOfSearch() == FindReferences::searchProject)
	{
		RWLockReader lck;
		const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);

		if (projMap.size() > 1)
		{
#ifdef _WIN64
			int tomtoImageId = ICONIDX_TOMATO;
#else
			int tomtoImageId = ICONIDX_TOMATO_BACKGROUND;
#endif // _WIN64

			HTREEITEM parentTreeItem = m_tree.InsertItemW(searched_project_node_text, tomtoImageId, tomtoImageId /*, TVI_ROOT*/);
			m_treeSubClass.SetItemFlags(parentTreeItem, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR |
			                                                TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
			m_tree.SetCheck(TVI_ROOT);
			m_tree.SetItemData(parentTreeItem, kFileRefBit);
			m_tree.SetItemState(parentTreeItem, TVIS_EXPANDED, TVIS_EXPANDED);
		}
	}
}

void FindUsageDlg::OnTreeEscape()
{
	if (gShellAttr->RequiresFindResultsHack() && !mHasClonedResults)
		OnCancel();
}

LRESULT
FindUsageDlg::OnTreeKeyDown(WPARAM wParam, LPARAM lParam)
{
	// [case: 82192]
	g_IgnoreBeepsTimer = GetTickCount() + 1000;

	// [case: 82047]
	if ('D' == wParam)
	{
		if (mRefTypes[FREF_Definition] || mRefTypes[FREF_DefinitionAssign])
			OnToggleFilterDefinitions();
		return 1;
	}

	if ('R' == wParam)
	{
		if (mRefTypes[FREF_Reference])
			OnToggleFilterReferences();
		return 1;
	}

	if ('W' == wParam)
	{
		if (mRefTypes[FREF_ReferenceAssign])
			OnToggleFilterReferenceAssigns();
		return 1;
	}

	if ('G' == wParam)
	{
		if (mRefTypes[FREF_Unknown])
			OnToggleFilterUnknowns();
		return 1;
	}

	if ('M' == wParam)
	{
		if (mRefTypes[FREF_Comment])
			OnToggleFilterComments();
		return 1;
	}

	if ('I' == wParam)
	{
		if (mRefTypes[FREF_IncludeDirective])
			OnToggleFilterIncludes();
		return 1;
	}

	if ('S' == wParam)
	{
		if (mRefTypes[FREF_ScopeReference])
			OnToggleFilterScopeReferences();
		return 1;
	}

	if ('O' == wParam)
	{
		if (mHasOverrides)
			OnToggleFilterInherited();
		return 1;
	}

	if ('P' == wParam)
	{
		OnToggleAllProjects();
		return 1;
	}

	if ('H' == wParam)
	{
		OnToggleHighlight();
		return 1;
	}

	if ('C' == wParam)
	{
		if (mRefTypes[FREF_Creation] || mRefTypes[FREF_Creation_Auto])
			OnToggleFilterCreations();
		return 1;
	}

	if ('A' == wParam || 'V' == wParam)
	{
		if (mRefTypes[FREF_ReferenceAutoVar] || mRefTypes[FREF_Creation_Auto])
			OnToggleFilterAutoVars();
		return 1;
	}

	// [case: 82192]
	g_IgnoreBeepsTimer = 0;
	return 0;
}

void FindUsageDlg::OnTreeKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
	// [case: 82047]
	NMTVKEYDOWN* nkd = (LPNMTVKEYDOWN)pNMHDR;
	LRESULT r = OnTreeKeyDown(nkd->wVKey, (LPARAM)nkd->flags);
	if (r)
	{
		nkd->wVKey = 0;
		*pResult = r;
	}
}

void FindUsageDlg::ReadLastSettingsFromRegistry()
{
	CRedirectRegistryToVA rreg;

	// if updated, synchronize with FindTextDlg.cpp
	SetFindText(CStringW(AfxGetApp()->GetProfileString("Find", "find", ""))); // XXX UNICODE
	SetFindCaseSensitive(!_stricmp(AfxGetApp()->GetProfileString("Find", "match_case", "no"), "yes"));
	SetMarkAll(!_stricmp(AfxGetApp()->GetProfileString("Find", "markall", "no"), "yes"));
	SetFindReverse(!_stricmp(AfxGetApp()->GetProfileString("Find", "direction", "down"), "up"));
}

void FindUsageDlg::BuildNavigationList(HTREEITEM item)
{
	if (TVI_ROOT == item)
	{
		mNavigationList.clear();
		mNavigationList.reserve(m_tree.GetCount());
	}
	else
		mNavigationList.push_back(NavItem(item, (int)m_tree.GetItemData(item)));

	HTREEITEM childItem = m_tree.GetChildItem(item);
	while (childItem)
	{
		if (m_tree.ItemHasChildren(childItem))
			BuildNavigationList(childItem);
		else
			mNavigationList.push_back(NavItem(childItem, (int)m_tree.GetItemData(childItem)));
		childItem = m_tree.GetNextSiblingItem(childItem);
	}
}

void FindUsageDlg::OnToggleFilterInherited()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return;

	__super::OnToggleFilterInherited();
	// TODO: toggle causes flicker back to unchecked state
	// reset button back to pressed state
	((CButton*)GetDlgItem(IDC_FIND_INHERITED_REFERENCES))
	    ->SetCheck(Psettings->mDisplayWiderScopeReferences ? BST_CHECKED : BST_UNCHECKED);
}

void FindUsageDlg::OnToggleAllProjects()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return;

	Psettings->mDisplayReferencesFromAllProjects = !Psettings->mDisplayReferencesFromAllProjects;
	auto btn = ((CButton*)GetDlgItem(IDC_FIND_REFERENCES_ALL_PROJECTS));
	btn->SetCheck(Psettings->mDisplayReferencesFromAllProjects ? BST_CHECKED : BST_UNCHECKED);

	CWaitCursor curs;
	OnRefresh();
}

void FindUsageDlg::OnToggleFilterComments()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return;

	__super::OnToggleFilterComments();
	// reset button back to pressed state
	// 	((CButton*)GetDlgItem(IDC_FIND_))->SetState(Psettings->mDisplayCommentAndStringReferences?BST_PUSHED:BST_UNCHECKED);
}

LRESULT FindUsageDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (CVS2010Colours::IsVS2010CommandBarColouringActive())
	{
		const COLORREF toolbarbg_cache = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_GRADIENT_BEGIN);

		switch (message)
		{
		case WM_ERASEBKGND: {
			HDC hdc = (HDC)wParam;
			CRect rect;
			GetClientRect(rect);
			CBrush brush;
			brush.CreateSolidBrush(toolbarbg_cache);
			::FillRect(hdc, rect, (HBRUSH)brush.m_hObject);
			return 1;
		}
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLOREDIT: {
			HDC hdc = (HDC)wParam;
			::SetBkColor(hdc, toolbarbg_cache);
			::SetTextColor(hdc, CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE));
			static CBrush ret;
			ret.DeleteObject();
			ret.CreateSolidBrush(toolbarbg_cache);
			return (LRESULT)(HBRUSH)ret;
		}
		}
	}

#ifndef RAD_STUDIO
	return ReferencesWndBase::WindowProc(message, wParam, lParam);
#else
// 	VADEBUGPRINT("#RAD " << (LPCSTR)ThemeUtils::GetWindowMessageString(message, wParam, lParam));
 	auto result = ReferencesWndBase::WindowProc(message, wParam, lParam);
// 	if ((!mParent && ::GetParent(m_hWnd)) || message == WM_WINDOWPOSCHANGED || message == CM_CONTROLCHANGE)
// 	{
// 		auto oldParent = mParent;
// 		mParent = ::GetParent(m_hWnd);
// 		if (oldParent != mParent && on_parent_changed)
// 		{
// 			on_parent_changed(oldParent, mParent);
// 		}
// 	}
 	return result;
#endif
}

WTString FindUsageDlg::GetCountString()
{
	WTString msg;
	WTString sym(mRefs->GetFindSym());
	const WTString origSym(mRefs->GetRenamedSym());
	if (!origSym.IsEmpty() && sym != origSym)
	{
		// [case: 73302] also show previous name if renamed after results captured
		sym += " / ";
		sym += origSym;
	}

	const int kDisplayedItems =
	    mReferencesCount -
	    mRefNodesRemoved; // don't use refs.Count since some items might be hidden by display settings
	if ((mRefs->flags & FREF_Flg_InFileOnly) && !(mRefs->flags & FREF_Flg_CorrespondingFile))
	{
		_ASSERTE(mFileCount < 2);
		if (mHiddenReferenceCount) // case 93368
		{
			msg.WTFormat("%s %d (+%ld hidden) reference%s to: %s (searched file)",
			             mRefNodesRemoved ? "Displaying" : "Found", kDisplayedItems, mHiddenReferenceCount,
			             kDisplayedItems == 1 ? "" : "s", sym.c_str());
		}
		else
		{
			msg.WTFormat("%s %d reference%s to: %s (searched file)", mRefNodesRemoved ? "Displaying" : "Found",
			             kDisplayedItems, kDisplayedItems == 1 ? "" : "s", sym.c_str());
		}
	}
	else
	{
		if (mHiddenReferenceCount) // case 93368
		{
			msg.WTFormat("%s %d (+%ld hidden) reference%s in %d file%s to: %s (searched %s)",
			             mRefNodesRemoved ? "Displaying" : "Found", kDisplayedItems, mHiddenReferenceCount,
			             kDisplayedItems == 1 ? "" : "s", (mFileCount - mFileNodesRemoved),
			             (mFileCount - mFileNodesRemoved) == 1 ? "" : "s", sym.c_str(),
			             mRefs->GetScopeOfSearchStr().c_str());
		}
		else
		{
			msg.WTFormat("%s %d reference%s in %d file%s to: %s (searched %s)",
			             mRefNodesRemoved ? "Displaying" : "Found", kDisplayedItems, kDisplayedItems == 1 ? "" : "s",
			             (mFileCount - mFileNodesRemoved), (mFileCount - mFileNodesRemoved) == 1 ? "" : "s",
			             sym.c_str(), mRefs->GetScopeOfSearchStr().c_str());
		}
	}
	return msg;
}

void FindUsageDlg::OnDpiChanged(DpiChange change, bool& handled)
{
	UINT dpiX, dpiY;
	if (change != CDpiAware::DpiChange::AfterParent &&
	    SUCCEEDED(VsUI::CDpiAwareness::GetDpiForWindow(m_hWnd, &dpiX, &dpiY)))
	{
		auto scope = VsUI::DpiHelper::SetDefaultForDPI(dpiX);

		UpdateFonts(VAFTF_All);

		CWnd* tmp = GetDlgItem(IDC_HIGHLIGHTAll);
		if (tmp)
			tmp->SetFont(&m_font);

		tmp = GetDlgItem(IDC_STATUS);
		if (tmp)
			tmp->SetFont(&m_font);

		tmp = GetDlgItem(IDCANCEL);
		if (tmp)
			tmp->SetFont(&m_font);

		PrepareImages();
		Layout();
	}

	if (change == CDpiAware::DpiChange::AfterParent)
		RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
}

void FindUsageDlg::OnFontSettingsChanged(VaFontTypeFlags changed, bool& handled)
{
	UpdateFonts(changed);

	CWnd* tmp = GetDlgItem(IDC_HIGHLIGHTAll);
	if (tmp)
		tmp->SetFont(&m_font);

	tmp = GetDlgItem(IDC_STATUS);
	if (tmp)
		tmp->SetFont(&m_font);

	tmp = GetDlgItem(IDCANCEL);
	if (tmp)
		tmp->SetFont(&m_font);
}

void FindUsageDlg::PrepareImages()
{
	WTString txt;
	txt = "Next Reference" + GetBindingTip("Edit.GoToNextLocation");
	SubstituteButton(0, IDC_NEXT, txt.c_str(), IDB_REFSNEXT, IDB_REFSNEXTSEL, 0, IDB_REFSNEXTDIS);
	txt = "Previous Reference" + GetBindingTip("Edit.GoToPrevLocation");
	SubstituteButton(1, IDC_PREVIOUS, txt.c_str(), IDB_REFSPREV, IDB_REFSPREVSEL, 0, IDB_REFSPREVDIS);
	SubstituteButton(2, IDC_REFRESH, "Refresh", IDB_REFSREFRESH, IDB_REFSREFRESHSEL, 0, IDB_REFREFRESHDIS);
	txt = "Find in Results" + GetBindingTip("Edit.Find");
	SubstituteButton(3, IDC_FINDINRESULTS, txt.c_str(), IDB_REFSFIND, IDB_REFSFINDSEL, 0, IDB_REFSFINDDIS);
	SubstituteButton(4, IDC_FIND_INHERITED_REFERENCES, "Display inherited and overridden references",
	                 IDB_TOGGLE_INHERITANCE, IDB_TOGGLE_INHERITANCESEL, 0, IDB_TOGGLE_INHERITANCEDIS, true);

	CButton* tmp = (CButton*)GetDlgItem(IDC_FIND_INHERITED_REFERENCES);
	if (tmp)
		tmp->SetCheck(Psettings->mDisplayWiderScopeReferences ? BST_CHECKED : BST_UNCHECKED);

	SubstituteButton(5, IDC_FIND_REFERENCES_ALL_PROJECTS, "Display references from all projects",
	                 IDB_TOGGLE_ALLPROJECTS, IDB_TOGGLE_ALLPROJECTSSEL, 0, IDB_TOGGLE_ALLPROJECTSDIS, true);

	tmp = (CButton*)GetDlgItem(IDC_FIND_REFERENCES_ALL_PROJECTS);
	if (tmp)
		tmp->SetCheck(Psettings->mDisplayReferencesFromAllProjects ? BST_CHECKED : BST_UNCHECKED);

	if (mHasClonedResults)
	{
		CWnd* tmp2 = GetDlgItem(IDC_CLONERESULTS);
		if (tmp2)
			tmp2->DestroyWindow();
	}
	else
		SubstituteButton(6, IDC_CLONERESULTS, "Clone Results", IDB_REFSCLONE, IDB_REFSCLONESEL, 0, IDB_REFSCLONEDIS);

	gImgListMgr->SetImgListForDPI(m_tree, ImageListManager::bgTree, TVSIL_NORMAL);
}
