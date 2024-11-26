// BorderCWnd.cpp : implementation file
//

#include "stdafxEd.h"
#include "EdCnt.h"
#include "BorderCWnd.h"
#include "BCMenu.h"
#include "VaMessages.h"
#include "..\addin\dscmds.h"
#include "resource.h"
#include "project.h"
#include "VATree.h"
#include <atlbase.h>
#include "FontSettings.h"
#include "VAFileView.h"
#include "AutotextManager.h"
#include "DevShellService.h"
#include "DevShellAttributes.h"
#include "VaService.h"
#include "VARefactor.h"
#include "file.h"
#include "mainThread.h"
#include "Settings.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "expansion.h"
#include "FileId.h"
#include "TokenW.h"
#include "StringUtils.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "..\common\TempAssign.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define IDT_RESET_COLOR 200

typedef struct MenuCommandData
{
	int id;
	union {
		HTREEITEM hItem;
		int mAutotextIdx;
	};
} MenuCommandData;

#define DYNAMIC_BASE_ID (WM_APP + 100)
#define IDM_EDITAUTOTEXT (DYNAMIC_BASE_ID + 50)
#define IDM_INSERTAUTOTEXT (DYNAMIC_BASE_ID + 51)
#define IDM_NEWAUTOTEXT (DYNAMIC_BASE_ID + 52)
#define MENUTEXT_EDIT_AUTOTEXT "&Edit VA Snippets..."
#define MENUTEXT_NEW_AUTOTEXT "&Create VA Snippet from selection..."

typedef CTypedPtrList<CPtrList, MenuCommandData*> MenuItemInfoList;
static MenuItemInfoList g_cb;

// context menu images are stored in contextMenu.bmp and loaded in BCGInit

// keep this in sync with the array beneath it and the array initialization in the BorderCWnd constructor
enum StaticContextMenuItem
{
	INS_REM_BRKPT = 0,
	ENABLE_BRKPT,
	BRKPT_PROPERTIES,
	REM_ALLBRKPTS,
	DISABLE_ALLBRKPTS,
	SEPARATOR_1,
	REM_BOOKMARKS,
	SEPARATOR_2,
	OPEN_MATCHED_FILE,
	FILE_IN_WORKSPACE_DLG,
	FIND_SYMBOL_DLG,
	ID_FindReferences,
	ID_AddMember,
	ID_AddSimilarMember,
	ID_CreateDeclaration,
	ID_CreateImplementation,
	ID_AddInclude,
	ID_ChangeSignature,
	ID_ChangeVisibility,
	ID_DocumentMethod,
	ID_EncapsulateField,
	ID_ExtractMethod,
	ID_MoveImplementation,
	ID_CreateFromUsage,
	ID_ImplementInterface,
	ID_Rename,
	ID_ExpandMacro,
	ID_AddRemoveBraces,
	ID_MoveImplementationToHdr,
	ID_ConvertBetweenPointerAndInstance,
	ID_SimplifyInstanceDeclaration,
	ID_AddForwardDeclaration,
	ID_ConvertEnum,
	NUMBER_OF_STATIC_MENU_ITEMS
};

#define DEV_LANG_ID 69

/////////////////////////////////////////////////////////////////////////////
// BorderCWnd
BorderCWnd::BorderCWnd(EdCntPtr ed)
{
	m_weakEd = ed;
	m_ed = ed.get();
	m_ContextMenu = NULL;

	m_line = 0;
	m_colored = false;
}

BorderCWnd::~BorderCWnd()
{
	DeleteMenu();
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(BorderCWnd, CWnd)
//{{AFX_MSG_MAP(BorderCWnd)
ON_WM_LBUTTONDOWN()
ON_WM_RBUTTONDOWN()
ON_WM_SETCURSOR()
ON_WM_PAINT()
ON_WM_TIMER()
ON_WM_LBUTTONUP()
ON_WM_SETFOCUS()
//}}AFX_MSG_MAP
ON_WM_MEASUREITEM()
ON_COMMAND_RANGE(WM_APP, WM_APP + NUMBER_OF_STATIC_MENU_ITEMS + 10, OnContextMenuSelection)
ON_COMMAND_RANGE(DYNAMIC_BASE_ID, 0xffff, OnContextMenuSelection)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// BorderCWnd message handlers

void BorderCWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	CWnd::OnLButtonDown(nFlags, point);

	// not needed now that the LButtonDown gets handled directly by the edit
	// modify flags so we know when to really do a select line or not
	//	nFlags |= MK_VA_SELLINE;
	//	m_ed->PostMessage(WM_LBUTTONDOWN, (WPARAM) nFlags, (LPARAM)MAKELPARAM(BORDER_SELECT_LINE, point.y));
}

void BorderCWnd::OnRButtonDown(UINT nFlags, CPoint point)
{
	CWnd::OnRButtonDown(nFlags, point);
	DisplayMenu(point);
}

void BorderCWnd::DeleteMenu()
{
	POSITION pos;
	MenuCommandData* tmpData;

	if ((pos = g_cb.GetHeadPosition()) != NULL)
	{
		do
		{
			tmpData = g_cb.GetAt(pos);
			delete tmpData;
		} while (g_cb.GetNext(pos) && pos);
	}
	g_cb.RemoveAll();

	delete m_ContextMenu;
	m_ContextMenu = NULL;
}

static void AddDynamicMenuItem(BCMenu* m, LPCSTR txt, int id, HTREEITEM hitem = NULL, int imgIdx = -1,
                               int extraStyle = 0)
{
	MenuCommandData* add = new MenuCommandData;
	add->id = id;
	add->hItem = hitem;
	g_cb.AddTail(add);
	if (txt && *txt)
		m->AppendODMenu(txt, UINT(MF_STRING | extraStyle), UINT_PTR(DYNAMIC_BASE_ID + g_cb.GetCount()), imgIdx);
	else
		m->AppendSeparator();
}

static void AddDynamicMenuItem(BCMenu* m, const WTString& txt, int id, HTREEITEM hitem = NULL, int imgIdx = -1,
                               int extraStyle = 0)
{
	AddDynamicMenuItem(m, txt.c_str(), id, hitem, imgIdx, extraStyle);
}

WTString WTLoadString(UINT id)
{
	WTString str;
	str.LoadString(id);
	return str;
}

void BorderCWnd::TrackPopup(const CPoint& pt)
{
	UINT pick;
	BOOL res;
	{
		TempTrue tt(m_ed->m_contextMenuShowing);
		res = m_ContextMenu->TrackPopupMenu(pt, (CWnd*)this, pick);
	}

	if (res)
		OnContextMenuSelection(pick);
}

BOOL BorderCWnd::DisplayMenu(CPoint point)
{
	m_line = m_ed->LineFromChar(m_ed->CharFromPos(&point));
	const WTString selstr(m_ed->GetSelString());
	DeleteMenu();
	m_ContextMenu = new BCMenu();
	m_ContextMenu->CreatePopupMenu();
	int itemCnt = 0;
	// gmit: 67174: GetDesktopEdges is ok like this since BrorderCWnd seems to be unused
	const CRect desktoprect = g_FontSettings->GetDesktopEdges(m_ed ? m_ed->m_hWnd : NULL);
	const int itemsPerCol = desktoprect.Height() / max(CMenuXP::GetItemHeight(m_hWnd), 10) -
	                        1; // gmit: 10 is used to prevent crash if GetItemHeight returns zero for whatever reason
	CMenuXP::ClearGlobals();
	PopulateStandardContextMenu(selstr, itemCnt, itemsPerCol);
	m_ed->vClientToScreen(&point);
	m_ed->PostMessage(WM_KEYDOWN, VK_DOWN, 1);
	TrackPopup(point);
	return TRUE;
}

void BorderCWnd::AddRefactoringItems()
{
	BCMenu* menu = new BCMenu;
	menu->CreatePopupMenu();

	const VARefactorCls ref;

	// add all refactor commands and gray out those that aren't applicable
	AddDynamicMenuItem(menu, "&Add Member...", ID_AddMember, NULL, -1,
	                   ref.CanRefactor(VARef_AddMember) ? 0 : MF_GRAYED);
	AddDynamicMenuItem(menu, "Add &Similar Member...", ID_AddSimilarMember, NULL, -1,
	                   ref.CanRefactor(VARef_AddSimilarMember) ? 0 : MF_GRAYED);
	AddDynamicMenuItem(menu, "&Create Declaration", ID_CreateDeclaration, NULL, -1,
	                   ref.CanRefactor(VARef_CreateMethodDecl) ? 0 : MF_GRAYED);
	{
		WTString menuText = "Create &Implementation";
		BOOL ok = ref.CanRefactor(VARef_CreateMethodImpl, &menuText);
		AddDynamicMenuItem(menu, menuText, ID_CreateImplementation, NULL, -1, ok ? 0 : MF_GRAYED);
	}
	AddDynamicMenuItem(menu, "Add I&nclude", ID_AddInclude, NULL, -1,
	                   ref.CanRefactor(VARef_AddInclude) ? 0 : MF_GRAYED);
	AddDynamicMenuItem(menu, "Change Si&gnature...", ID_ChangeSignature, NULL, -1,
	                   ref.CanRefactor(VARef_ChangeSignature) ? 0 : MF_GRAYED);
	// 	AddDynamicMenuItem(menu, "Change &Visibility...", ID_ChangeVisibility, NULL, -1,
	// 		ref.CanRefactor(VARef_ChangeVisibility) ? 0 : MF_GRAYED);
	AddDynamicMenuItem(menu, "&Document Method", ID_DocumentMethod, NULL, -1,
	                   ref.CanRefactor(VARef_CreateMethodComment) ? 0 : MF_GRAYED);
	AddDynamicMenuItem(menu, "Encapsulate &Field", ID_EncapsulateField, NULL, -1,
	                   ref.CanRefactor(VARef_EncapsulateField) ? 0 : MF_GRAYED);
	if (m_ed && IsCFile(m_ed->m_ftype))
	{
		WTString menuText = "C&onvert Between Pointer and Instance";
		BOOL ok = ref.CanRefactor(VARef_ConvertBetweenPointerAndInstance, &menuText);
		AddDynamicMenuItem(menu, menuText, ID_ConvertBetweenPointerAndInstance, NULL, -1, ok ? 0 : MF_GRAYED);
	}
	if (m_ed && IsCFile(m_ed->m_ftype))
	{
		WTString menuText = "&Convert Unscoped Enum to Scoped Enum";
		BOOL ok = ref.CanRefactor(VARef_ConvertEnum);
		AddDynamicMenuItem(menu, menuText, ID_ConvertEnum, NULL, -1, ok ? 0 : MF_GRAYED);
	}
	if (m_ed && IsCFile(m_ed->m_ftype))
	{
		WTString menuText = "Sim&plify Instance Definition";
		BOOL ok = ref.CanRefactor(VARef_SimplifyInstance);
		AddDynamicMenuItem(menu, menuText, ID_SimplifyInstanceDeclaration, NULL, -1, ok ? 0 : MF_GRAYED);
	}
	if (m_ed && IsCFile(m_ed->m_ftype))
	{
		WTString menuText = "Add For&ward Declaration";
		BOOL ok = ref.CanRefactor(VARef_AddForwardDeclaration);
		AddDynamicMenuItem(menu, menuText, ID_AddForwardDeclaration, NULL, -1, ok ? 0 : MF_GRAYED);
	}
	AddDynamicMenuItem(menu, "&Extract Method...", ID_ExtractMethod, NULL, -1,
	                   ref.CanRefactor(VARef_ExtractMethod) ? 0 : MF_GRAYED);
	if (m_ed && IsCFile(m_ed->m_ftype))
	{
		{
			WTString menuText = "&Move Implementation to Source File";
			BOOL ok = ref.CanRefactor(VARef_MoveImplementationToSrcFile, &menuText);
			AddDynamicMenuItem(menu, menuText, ID_MoveImplementation, NULL, -1, ok ? 0 : MF_GRAYED);
		}
		{
			WTString menuText = "Move Implementation to &Header File";
			BOOL ok = ref.CanRefactor(VARef_MoveImplementationToHdrFile, &menuText);
			AddDynamicMenuItem(menu, menuText, ID_MoveImplementationToHdr, NULL, -1, ok ? 0 : MF_GRAYED);
		}
#ifdef _DEBUG
		AddDynamicMenuItem(menu, "Expand Macro", ID_ExpandMacro, NULL, -1,
		                   ref.CanRefactor(VARef_ExpandMacro) ? 0 : MF_GRAYED);
#endif
	}
	AddDynamicMenuItem(menu, "Create From &Usage...", ID_CreateFromUsage, NULL, -1,
	                   ref.CanRefactor(VARef_CreateFromUsage) ? 0 : MF_GRAYED);
	{
		WTString menuText;
		BOOL ok = ref.CanRefactor(VARef_ImplementInterface, &menuText);
		AddDynamicMenuItem(menu, menuText, ID_ImplementInterface, NULL, -1, ok ? 0 : MF_GRAYED);
	}
	{
		WTString menuText;
		BOOL ok = ref.CanRefactor(VARef_AddRemoveBraces, &menuText);
		AddDynamicMenuItem(menu, menuText, ID_AddRemoveBraces, NULL, -1, ok ? 0 : MF_GRAYED);
	}
	AddDynamicMenuItem(menu, "&Rename...", ID_Rename, NULL, -1, ref.CanRefactor(VARef_Rename) ? 0 : MF_GRAYED);

	m_ContextMenu->AppendPopup("&Refactor", MF_POPUP, menu);
	m_ContextMenu->AppendSeparator();
}

void BorderCWnd::PopulateAutotextMenu(BCMenu* menu, int itemsPerCol, bool standaloneMenu)
{
	// duplicated by EdCnt::CodeTemplateMenu
	const BOOL kHasSelection = m_ed->HasSelection();
	gAutotextMgr->Load(m_ed->m_ScopeLangType);

	int atUsingSelectionCnt, atWithShortcutCnt, atMoreCnt, atClipboardCnt;
	atUsingSelectionCnt = atWithShortcutCnt = atMoreCnt = atClipboardCnt = 0;

	BCMenu* atUsingSelectionMenu = new BCMenu;
	BCMenu* atWithShortcutsMenu = new BCMenu;
	BCMenu* atUsingClipboardMenu = new BCMenu;
	BCMenu* atMoreMenu = new BCMenu;

	atUsingSelectionMenu->CreateMenu();
	atWithShortcutsMenu->CreateMenu();
	atMoreMenu->CreateMenu();
	atUsingClipboardMenu->CreateMenu();

	// if editor has no selection:
	// A list of entries with titles and no shortcuts and no selection
	// Autotext using $selected$
	// Autotext with shortcuts
	// Edit Autotext..
	//
	// if editor has selection:
	// A list of titles of entries that use $selected$
	// More Autotext
	// Edit Autotext...

	int mainItemsAdded;
	int curItemIdx;
	const int kItems = gAutotextMgr->GetCount();
	for (mainItemsAdded = curItemIdx = 0; curItemIdx < kItems; ++curItemIdx)
	{
		const bool hasShortcut = gAutotextMgr->HasShortcut(curItemIdx);
		const bool usesSelection = gAutotextMgr->DoesItemUseString(curItemIdx, kAutotextKeyword_Selection);
		const bool usesClipboard = gAutotextMgr->DoesItemUseString(curItemIdx, kAutotextKeyword_Clipboard);

		WTString menuItemTxt(gAutotextMgr->GetTitle(curItemIdx, true));
		if (menuItemTxt.IsEmpty())
			continue;
		if (menuItemTxt.Find('&') == -1)
		{
			const int itemLen = menuItemTxt.GetLength();
			for (int idx = 0; idx < itemLen; ++idx)
			{
				if (::wt_isalnum(menuItemTxt[idx]))
				{
					menuItemTxt = menuItemTxt.Left(idx) + '&' + menuItemTxt.Mid(idx);
					break;
				}
			}
		}
		if (hasShortcut)
			menuItemTxt += "\t" + gAutotextMgr->GetShortcut(curItemIdx);

		if (kHasSelection)
		{
			if (usesSelection)
			{
				const int style = ++mainItemsAdded % itemsPerCol ? 0 : MF_MENUBARBREAK;
				AddDynamicMenuItem(menu, menuItemTxt, IDM_INSERTAUTOTEXT, (HTREEITEM)(intptr_t)curItemIdx, -1, style);
			}
			else
			{
				const int style = ++atMoreCnt % itemsPerCol ? 0 : MF_MENUBARBREAK;
				AddDynamicMenuItem(atMoreMenu, menuItemTxt, IDM_INSERTAUTOTEXT, (HTREEITEM)(intptr_t)curItemIdx, -1,
				                   style);
			}
		}
		else
		{
			if (usesSelection)
			{
				const int style = ++atUsingSelectionCnt % itemsPerCol ? 0 : MF_MENUBARBREAK;
				AddDynamicMenuItem(atUsingSelectionMenu, menuItemTxt, IDM_INSERTAUTOTEXT,
				                   (HTREEITEM)(intptr_t)curItemIdx, -1, style);
			}

			if (usesClipboard)
			{
				const int style = ++atClipboardCnt % itemsPerCol ? 0 : MF_MENUBARBREAK;
				AddDynamicMenuItem(atUsingClipboardMenu, menuItemTxt, IDM_INSERTAUTOTEXT,
				                   (HTREEITEM)(intptr_t)curItemIdx, -1, style);
			}

			if (hasShortcut)
			{
				const int style = ++atWithShortcutCnt % itemsPerCol ? 0 : MF_MENUBARBREAK;
				AddDynamicMenuItem(atWithShortcutsMenu, menuItemTxt, IDM_INSERTAUTOTEXT,
				                   (HTREEITEM)(intptr_t)curItemIdx, -1, style);
			}

			if (!hasShortcut && !usesSelection && !usesClipboard)
			{
				const int style = ++mainItemsAdded % itemsPerCol ? 0 : MF_MENUBARBREAK;
				AddDynamicMenuItem(menu, menuItemTxt, IDM_INSERTAUTOTEXT, (HTREEITEM)(intptr_t)curItemIdx, -1, style);
			}
		}
	}

	if (atUsingSelectionCnt || atWithShortcutCnt || atMoreCnt || standaloneMenu || atClipboardCnt)
		menu->AppendSeparator();

	if (atMoreCnt)
	{
		_ASSERTE(atMoreMenu->GetMenuItemCount());
		menu->AppendPopup("&More VA Snippets", MF_POPUP, atMoreMenu);
	}
	else
	{
		_ASSERTE(!atMoreMenu->GetMenuItemCount());
		delete atMoreMenu;
	}

	if (atClipboardCnt)
	{
		_ASSERTE(atUsingClipboardMenu->GetMenuItemCount());
		CString txt("V&A Snippets with ");
		txt += kAutotextKeyword_Clipboard;
		menu->AppendPopup(txt, MF_POPUP, atUsingClipboardMenu);
	}
	else
	{
		_ASSERTE(!atUsingClipboardMenu->GetMenuItemCount());
		delete atUsingClipboardMenu;
	}

	if (atUsingSelectionCnt)
	{
		_ASSERTE(atUsingSelectionMenu->GetMenuItemCount());
		CString txt("V&A Snippets with ");
		txt += kAutotextKeyword_Selection;
		menu->AppendPopup(txt, MF_POPUP, atUsingSelectionMenu);
	}
	else
	{
		_ASSERTE(!atUsingSelectionMenu->GetMenuItemCount());
		delete atUsingSelectionMenu;
	}

	if (atWithShortcutCnt)
	{
		_ASSERTE(atWithShortcutsMenu->GetMenuItemCount());
		menu->AppendPopup("V&A Snippets with shortcuts", MF_POPUP, atWithShortcutsMenu);
	}
	else
	{
		_ASSERTE(!atWithShortcutsMenu->GetMenuItemCount());
		delete atWithShortcutsMenu;
	}

	if (standaloneMenu)
	{
		AddDynamicMenuItem(menu, MENUTEXT_NEW_AUTOTEXT, IDM_NEWAUTOTEXT, NULL, -1, kHasSelection ? 0 : MF_GRAYED);
		AddDynamicMenuItem(menu, MENUTEXT_EDIT_AUTOTEXT, IDM_EDITAUTOTEXT);
	}
}

BOOL BorderCWnd::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	SetCursor(LoadCursor(0, IDC_ARROW));
	return true;
}

void BorderCWnd::OnPaint()
{
	ShowWindow(SW_HIDE);
	EnableWindow(FALSE);
	{
		// put in block to free dc before Format
		CPaintDC dc(this); // device context for painting
	}
	// m_ed->GetAttrLst()->Format(m_ed, 0, NPOS, m_colored, m_flashColor);
}

void BorderCWnd::Flash(COLORREF color /* = RGB(0,0,255)*/, DWORD duration /* = 3000 */)
{
	if (color == (COLORREF)-1)
	{
		if (m_colored)
		{
			m_colored = false;
			CRect edrect;
			m_ed->vGetClientRect(edrect);
			edrect.right = BORDER_WIDTH + FRAME_WIDTH + 1;
			::InvalidateRect(m_ed->m_hWnd, &edrect, TRUE);
		}
		return;
	}
	Invalidate(true);
	{
		CPaintDC dc(this); // device context for painting

		// draw/fill border
		CRect edrect;
		m_ed->vGetClientRect(edrect);
		edrect.right = BORDER_WIDTH + FRAME_WIDTH;
		dc.FillSolidRect(edrect, color);
	}
	m_colored = true;
	SetTimer(IDT_RESET_COLOR, duration, NULL);
}

void BorderCWnd::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case IDT_RESET_COLOR:
		KillTimer(nIDEvent);
		if (m_colored)
		{
			m_colored = false;
			CRect edrect;
			m_ed->vGetClientRect(edrect);
			edrect.right = BORDER_WIDTH + FRAME_WIDTH + 1;
			::InvalidateRect(m_ed->m_hWnd, &edrect, TRUE);
		}
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void BorderCWnd::OnContextMenuSelection(UINT pick)
{
	if (pick >= DYNAMIC_BASE_ID)
	{
		MenuCommandData* cbDat = NULL;
		UINT idx = pick - DYNAMIC_BASE_ID, idx2 = 0;
		POSITION pos;
		if ((pos = g_cb.GetHeadPosition()) != NULL)
		{
			while (++idx2 < idx && g_cb.GetNext(pos) && pos)
				;
			if (idx2 == idx)
				cbDat = g_cb.GetAt(pos);
		}
		if (!cbDat)
		{
			ASSERT(FALSE);
			return;
		}

		if (cbDat->id == DSM_GOTO)
		{
			::PostMessage(*m_ed, WM_COMMAND, WM_VA_DEFINITIONLIST, 0);
			return;
		}
		else if (cbDat->id == IDM_NEWAUTOTEXT)
		{
			WTString sel = m_ed->GetSelString();
			gAutotextMgr->Edit(m_ed->m_ScopeLangType, NULL, sel);
		}
		else if (cbDat->id == IDM_EDITAUTOTEXT)
		{
			gAutotextMgr->Edit(m_ed->m_ScopeLangType);
		}
		else if (IDM_INSERTAUTOTEXT == cbDat->id)
		{ // select item
			EdCntPtr ed = m_weakEd.lock();
			gAutotextMgr->Insert(ed, cbDat->mAutotextIdx);
			return;
		}
		else if (cbDat->hItem)
		{ // select item
			EdCntPtr ed = m_weakEd.lock();
			g_VATabTree->OpenItemFile(cbDat->hItem, ed.get());
			return;
		}
		else if (cbDat->id == DSM_FORMAT)
			gShellSvc->FormatSelection();
		pick = (UINT)cbDat->id;
	}
	else
		pick -= WM_APP;

	switch (pick)
	{
	case FILE_IN_WORKSPACE_DLG:
		if (gVaService)
			gVaService->Exec(IVaService::ct_global, icmdVaCmd_OpenFileInWorkspaceDlg);
		break;
	case ID_FindReferences:
		Refactor(VARef_FindUsage);
		break;
	case FIND_SYMBOL_DLG:
		if (gVaService)
			gVaService->Exec(IVaService::ct_global, icmdVaCmd_FindSymbolDlg);
		break;
	case INS_REM_BRKPT: // "Insert/Remove Breakpoint"
		m_ed->SetPos(m_ed->LinePos(m_line));
		gShellSvc->ToggleBreakpoint();
		break;
	case BRKPT_PROPERTIES:
		m_ed->SetPos(m_ed->LinePos(m_line));
		gShellSvc->BreakpointProperties();
		break;
	case ENABLE_BRKPT:
		m_ed->SetPos(m_ed->LinePos(m_line));
		gShellSvc->EnableBreakpoint();
		break;
	case REM_BOOKMARKS:
		if (!gShellSvc->ClearBookmarks(*m_ed))
			m_ed->CmUnMarkAll();
		break;
	case REM_ALLBRKPTS:
		gShellSvc->RemoveAllBreakpoints();
		return;
	case DISABLE_ALLBRKPTS:
		gShellSvc->DisableAllBreakpoints();
		return;
	case OPEN_MATCHED_FILE:
		if (m_openFileName.GetLength())
			DelayFileOpen(m_openFileName, 0, NULL, TRUE);
		else
			::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, ID_FILE_OPEN, 0);
		return;
	case ID_AddMember:
		Refactor(VARef_AddMember);
		break;
	case ID_AddSimilarMember:
		Refactor(VARef_AddSimilarMember);
		break;
	case ID_ChangeSignature:
		Refactor(VARef_ChangeSignature);
		break;
	case ID_ChangeVisibility:
		Refactor(VARef_ChangeVisibility);
		break;
	case ID_CreateDeclaration:
		Refactor(VARef_CreateMethodDecl);
		break;
	case ID_CreateImplementation:
		Refactor(VARef_CreateMethodImpl);
		break;
	case ID_AddInclude:
		Refactor(VARef_AddInclude);
		break;
	case ID_DocumentMethod:
		Refactor(VARef_CreateMethodComment);
		break;
	case ID_EncapsulateField:
		Refactor(VARef_EncapsulateField);
		break;
	case ID_ExtractMethod:
		Refactor(VARef_ExtractMethod);
		break;
	case ID_MoveImplementation:
		Refactor(VARef_MoveImplementationToSrcFile);
		break;
	case ID_CreateFromUsage:
		Refactor(VARef_CreateFromUsage);
		break;
	case ID_ImplementInterface:
		Refactor(VARef_ImplementInterface);
		break;
	case ID_Rename:
		Refactor(VARef_Rename);
		break;
	case ID_ExpandMacro:
		Refactor(VARef_ExpandMacro);
		break;
	case ID_AddRemoveBraces:
		Refactor(VARef_AddRemoveBraces);
		break;
	case ID_MoveImplementationToHdr:
		Refactor(VARef_MoveImplementationToHdrFile);
		break;
	case ID_ConvertBetweenPointerAndInstance:
		Refactor(VARef_ConvertBetweenPointerAndInstance);
		break;
	case ID_SimplifyInstanceDeclaration:
		Refactor(VARef_SimplifyInstance);
		break;
	case ID_AddForwardDeclaration:
		Refactor(VARef_AddForwardDeclaration);
		break;
	case ID_ConvertEnum:
		Refactor(VARef_ConvertEnum);
		break;
	default:
		::PostMessage(*m_ed, WM_COMMAND, pick, 0);
	}
	Invalidate();
}

void BorderCWnd::DrawBkGnd(bool dopaint)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	// resize wnd
	CRect r;
	GetParent()->GetClientRect(&r);
	int offset = 0;
	if (Psettings->m_enableVA)
		offset = (int)Psettings->m_minihelpHeight;
	if (Psettings->minihelpAtTop)
	{
		r.top += offset + VsUI::DpiHelper::LogicalToDeviceUnitsY(FRAME_WIDTH);
		r.bottom -= VsUI::DpiHelper::LogicalToDeviceUnitsY(FRAME_WIDTH);
	}
	else
	{
		r.top += VsUI::DpiHelper::LogicalToDeviceUnitsY(FRAME_WIDTH);
		r.bottom -= offset + VsUI::DpiHelper::LogicalToDeviceUnitsY(FRAME_WIDTH);
	}
	r.right = VsUI::DpiHelper::LogicalToDeviceUnitsX(BORDER_WIDTH + FRAME_WIDTH);
	r.left = VsUI::DpiHelper::LogicalToDeviceUnitsX(FRAME_WIDTH);
	MoveWindow(&r, false);
	Invalidate(false);
	if (!gShellAttr->IsDevenv() && dopaint && g_mainThread == GetCurrentThreadId())
		OnPaint();
}

void BorderCWnd::OnSetFocus(CWnd* pOldWnd)
{
	// sometimes this would end up with focus after pressing alt, alt, alt??
	m_ed->vSetFocus();
}

void BorderCWnd::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	BOOL setflag = FALSE;
	if (lpMeasureItemStruct->CtlType == ODT_MENU)
	{
		if (IsMenu((HMENU)(uintptr_t)lpMeasureItemStruct->itemID))
		{
			// this is no good
			//			CMenu* cmenu=CMenu::FromHandle((HMENU)lpMeasureItemStruct->itemID);
			//			if(m_ContextMenu && m_ContextMenu->IsMenu(cmenu)){
			m_ContextMenu->MeasureItem(lpMeasureItemStruct);
			setflag = TRUE;
			//			}
		}
	}
	if (!setflag)
		CWnd::OnMeasureItem(nIDCtl, lpMeasureItemStruct);
}

void BorderCWnd::PopulateStandardContextMenu(const WTString& selstr, int& itemCnt, const int itemsPerCol)
{
	int style = 0;
	if (!selstr.GetLength())
		style = MF_GRAYED;

	if (gShellAttr->SupportsBreakpoints())
	{
		AddDynamicMenuItem(m_ContextMenu, WTLoadString(IDS_BORDERMENU_INS_BRKPT), INS_REM_BRKPT, NULL,
		                   ICONIDX_BREAKPOINT_ADD);
		AddDynamicMenuItem(m_ContextMenu, "Enable/Disable Breakpoint", ENABLE_BRKPT, NULL, ICONIDX_BREAKPOINT_DISABLE);
		if (gShellAttr->SupportsBreakpointProperties())
			AddDynamicMenuItem(m_ContextMenu, _T("&Breakpoint Properties..."), BRKPT_PROPERTIES);
		AddDynamicMenuItem(m_ContextMenu, WTLoadString(IDS_BORDERMENU_REM_ALLBRKPTS), REM_ALLBRKPTS, NULL,
		                   ICONIDX_BREAKPOINT_REMOVE_ALL);
		AddDynamicMenuItem(m_ContextMenu, WTLoadString(IDS_BORDERMENU_DISABLE_ALLBRKPTS), DISABLE_ALLBRKPTS, NULL,
		                   ICONIDX_BREAKPOINT_DISABLE_ALL);
	}
	AddDynamicMenuItem(m_ContextMenu, WTLoadString(IDS_BORDERMENU_REM_BOOKMARKS), REM_BOOKMARKS, NULL,
	                   ICONIDX_BOOKMARK_REMOVE);
	m_ContextMenu->AppendSeparator();
	// create on the fly - to match the current file but change extension
	m_openFileName = m_ed->FileName();
	if (SwapExtension(m_openFileName) && m_openFileName.GetLength())
	{
		WTString txt;
		txt.FormatMessage(IDS_BORDERMENU_OPENFILENAME, (LPCTSTR)CString(Basename(m_openFileName)));
		m_ContextMenu->AppendODMenu(txt.c_str(), MF_STRING, WM_APP + OPEN_MATCHED_FILE, ICONIDX_OPEN_OPPOSITE);
	}
	if (gShellAttr->IsMsdev())
		AddDynamicMenuItem(m_ContextMenu, "&Open File in Workspace", FILE_IN_WORKSPACE_DLG, NULL, ICONIDX_FIW);
	else
		AddDynamicMenuItem(m_ContextMenu, "Open File in Sol&ution", FILE_IN_WORKSPACE_DLG, NULL, ICONIDX_FIW);
	AddDynamicMenuItem(m_ContextMenu, "Find S&ymbol", FIND_SYMBOL_DLG, NULL, ICONIDX_SIW);
	AddDynamicMenuItem(m_ContextMenu, "Fin&d References", ID_FindReferences, NULL, ICONIDX_REFERENCE_FIND_REF,
	                   CanRefactor(VARef_FindUsage) ? 0 : MF_GRAYED);
	m_ContextMenu->AppendSeparator();
	AddRefactoringItems();

	if (g_VATabTree)
	{
		BCMenu* menu = NULL;
		m_ContextMenu->AppendSeparator();
		gAutotextMgr->Load(m_ed->m_ScopeLangType);
		HTREEITEM mitem = g_VATabTree->GetChildItem(TVI_ROOT);
		if (mitem)
			mitem = g_VATabTree->GetNextItem(mitem, TVGN_CHILD);
		for (; mitem; mitem = g_VATabTree->GetNextItem(mitem, TVGN_NEXT))
		{
			bool addSeparator = false;
			WTString title = (LPCSTR)g_VATabTree->GetItemText(mitem);
			if (title == VAT_BOOKMARK)
			{
				if (!gShellAttr->SupportsBookmarks())
					continue;
			}

			menu = m_ContextMenu->AddDynamicMenu(title.c_str(), -1);
			WTString menuTxt;
			HTREEITEM item = g_VATabTree->GetChildItem(mitem);
			itemCnt = 0;
			while (item)
			{
				// start a new column every x items
				extern bool IncrementAndCheckIfMenubarBreak(int& items, int items_per_column);
				int style2 = IncrementAndCheckIfMenubarBreak(itemCnt, itemsPerCol) ? MF_MENUBARBREAK : 0;
				menuTxt = ::BuildMenuTextHexAccelerator((uint)itemCnt, g_VATabTree->GetItemText(item));
				AddDynamicMenuItem(menu, menuTxt.c_str(), -1, item, -1, style2);
				item = g_VATabTree->GetNextItem(item, TVGN_NEXT);
			}

			if (title == VAT_PASTE)
			{
				// add autotext after paste
				menu = m_ContextMenu->AddDynamicMenu("Insert V&A Snippet", ICONIDX_VATEMPLATE);
				PopulateAutotextMenu(menu, itemsPerCol, false);
				AddDynamicMenuItem(m_ContextMenu, MENUTEXT_NEW_AUTOTEXT, IDM_NEWAUTOTEXT, NULL, -1, style);
				AddDynamicMenuItem(m_ContextMenu, MENUTEXT_EDIT_AUTOTEXT, IDM_EDITAUTOTEXT);
				addSeparator = true;
			}

			if (addSeparator)
				m_ContextMenu->AppendSeparator();
		}

		// display macros always, invalidate if not no selection
		m_ContextMenu->AppendSeparator();
		int lang = g_currentEdCnt ? g_currentEdCnt->m_ScopeLangType : 0;
		if (Is_VB_VBS_File(lang))
		{
			AddDynamicMenuItem(m_ContextMenu, "&'...", VAM_COMMENTBLOCK2, NULL, -1, style);
			AddDynamicMenuItem(m_ContextMenu, "U&ncomment Lines ('...)", VAM_UNCOMMENTBLOCK2, NULL, -1, style);
		}
		else if (Is_Tag_Based(lang))
		{
			// TODO: Add comment out in <!-- HTML -->
		}
		else
		{
			if (selstr.contains("/ *") || selstr.contains("/*"))
			{
				AddDynamicMenuItem(m_ContextMenu, "/&*...*/", VAM_COMMENTBLOCK, NULL, ICONIDX_SNIPPET_COMMENTBLOCK,
				                   style | MF_GRAYED);
				AddDynamicMenuItem(m_ContextMenu, "Uncomment Block (/*...*/)", VAM_UNCOMMENTBLOCK, NULL,
				                   ICONIDX_SNIPPET_COMMENTBLOCK, style);
			}
			else
			{
				AddDynamicMenuItem(m_ContextMenu, "/&*...*/", VAM_COMMENTBLOCK, NULL, -1, style);
				AddDynamicMenuItem(m_ContextMenu, "U&ncomment Block (/*...*/)", VAM_UNCOMMENTBLOCK, NULL,
				                   ICONIDX_SNIPPET_COMMENTBLOCK, style | MF_GRAYED);
			}
			AddDynamicMenuItem(m_ContextMenu, "&// ...", VAM_COMMENTBLOCK2, NULL, -1, style);
			if (selstr.contains("\n//") || selstr.Find("//") == 0)
				AddDynamicMenuItem(m_ContextMenu, "U&ncomment Lines (// ...)", VAM_UNCOMMENTBLOCK2, NULL,
				                   ICONIDX_SNIPPET_COMMENTLINE, style);
			else
				AddDynamicMenuItem(m_ContextMenu, "U&ncomment Lines (// ...)", VAM_UNCOMMENTBLOCK2, NULL,
				                   ICONIDX_SNIPPET_COMMENTLINE, style | MF_GRAYED);
		}

		if (selstr.contains("\n"))
		{
			AddDynamicMenuItem(m_ContextMenu, "&{ ... }", VAM_ADDBRACE, NULL, ICONIDX_SNIPPET_BRACKETS, style);
			AddDynamicMenuItem(m_ContextMenu, "&(...)", VAM_PARENBLOCK, NULL, ICONIDX_SNIPPET_PARENS,
			                   style | MF_GRAYED);
			if (CS == m_ed->m_ftype)
				AddDynamicMenuItem(m_ContextMenu, "&#region ... #endregion", VAM_REGIONBLOCK, NULL, -1, style);
			else if (Is_VB_VBS_File(m_ed->m_ftype))
				AddDynamicMenuItem(m_ContextMenu, "&#Region ... #End Region", VAM_REGIONBLOCK, NULL, -1, style);
			else if (IsCFile(m_ed->m_ftype))
			{
				if (gShellAttr->IsDevenv8OrHigher())
					AddDynamicMenuItem(m_ContextMenu, "&#pragma region ... #pragma endregion", VAM_REGIONBLOCK, NULL,
					                   -1, style);
				AddDynamicMenuItem(m_ContextMenu, "&#ifdef ... #endif", VAM_IFDEFBLOCK, NULL, ICONIDX_SNIPPET_IFDEF,
				                   style);
			}
			if (gShellAttr->SupportsSelectionReformat())
				AddDynamicMenuItem(m_ContextMenu, "Reforma&t Selection", DSM_FORMAT, NULL, -1, style);
			AddDynamicMenuItem(m_ContextMenu, "&Sort Selected Lines", VAM_SORTSELECTION, NULL, -1, style);
		}
		else
		{
			AddDynamicMenuItem(m_ContextMenu, "&{ ... }", VAM_ADDBRACE, NULL, ICONIDX_SNIPPET_BRACKETS,
			                   style | MF_GRAYED);
			AddDynamicMenuItem(m_ContextMenu, "&(...)", VAM_PARENBLOCK, NULL, ICONIDX_SNIPPET_PARENS, style);
			if (CS == m_ed->m_ftype)
				AddDynamicMenuItem(m_ContextMenu, "&#region ... #endregion", VAM_REGIONBLOCK, NULL, -1,
				                   style | MF_GRAYED);
			else if (Is_VB_VBS_File(m_ed->m_ftype))
				AddDynamicMenuItem(m_ContextMenu, "&#Region ... #End Region", VAM_REGIONBLOCK, NULL, -1,
				                   style | MF_GRAYED);
			else if (IsCFile(m_ed->m_ftype))
			{
				if (gShellAttr->IsDevenv8OrHigher())
					AddDynamicMenuItem(m_ContextMenu, "&#pragma region ... #pragma endregion", VAM_REGIONBLOCK, NULL,
					                   -1, style | MF_GRAYED);
				AddDynamicMenuItem(m_ContextMenu, "&#ifdef ... #endif", VAM_IFDEFBLOCK, NULL, -1, style | MF_GRAYED);
			}
			if (gShellAttr->SupportsSelectionReformat())
				AddDynamicMenuItem(m_ContextMenu, "Reforma&t Selection", DSM_FORMAT, NULL, -1, style | MF_GRAYED);
			AddDynamicMenuItem(m_ContextMenu, "&Sort Selected Lines", VAM_SORTSELECTION, NULL, -1, style | MF_GRAYED);
		}
	}
}

void BorderCWnd::PopulatePasteMenu(int& itemCnt, const int itemsPerCol)
{
	// duplicated by EdCnt::CmEditPasteMenu
	HTREEITEM mitem = g_VATabTree->GetChildItem(TVI_ROOT);
	if (mitem)
		mitem = g_VATabTree->GetNextItem(mitem, TVGN_CHILD);
	while (mitem)
	{
		WTString title = (LPCSTR)g_VATabTree->GetItemText(mitem);
		if (title == VAT_PASTE)
		{
			WTString menuTxt;
			HTREEITEM item = g_VATabTree->GetChildItem(mitem);
			itemCnt = 0;
			while (item)
			{
				// start a new column every x items
				int style = ++itemCnt % itemsPerCol ? 0 : MF_MENUBARBREAK;
				menuTxt = ::BuildMenuTextHexAccelerator((uint)itemCnt, g_VATabTree->GetItemText(item));
				AddDynamicMenuItem(m_ContextMenu, menuTxt.c_str(), -1, item, -1, style);
				item = g_VATabTree->GetNextItem(item, TVGN_NEXT);
			}
			break;
		}
		mitem = g_VATabTree->GetNextItem(mitem, TVGN_NEXT);
	}
}
