#include "stdafxed.h"
#include "resource.h"
#include "BCMenu.h" // BCMenu class declaration
#pragma warning(push, 2)
#include "Business Components Gallery\BCGControlBar\bcgcb.h" // BCG Control Bar
#pragma warning(pop)
#include "DevShellAttributes.h"
#include "WTString.h"
#include "Armadillo\Armadillo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void BCGInit(BOOL init)
{
	if (init)
	{
		CBCGWorkspace::UseWorkspaceManager(_T("VA"));

		CBCGToolBar::SetMenuSizes(CSize(26, 21), CSize(16, 15));
		CBCGToolBar::SetHotTextColor(::GetSysColor(COLOR_HIGHLIGHT));
		CBCGToolBar pTb;
		CBCGToolBar::GetImages()->SetTransparentColor(0x00ff00);
		if (gShellAttr->ShouldUseXpVisualManager())
		{
			CBCGPopupMenu::SetForceShadow(TRUE);
			CBCGVisualManager::SetDefaultManager(RUNTIME_CLASS(CBCGVisualManagerXP));
		}
		else
		{
			CBCGVisualManager::SetDefaultManager(NULL);
			CBCGMenuBar::EnableMenuShadows(FALSE);
		}
		CBCGVisualManager::GetInstance();
		globalData.clrBtnFace = 0x00ff00;
	}
	else
	{
		BCGCBCleanUp();
	}
};

void LoadBcgBitmap()
{
	CBCGToolBar pTb;
	CBCGToolBar::GetImages()->SetTransparentColor(0x00ff00);
	// doesn't look like the version of BCG we use supports 32bit bitmap alpha channel
	// continue to use old icons in vs2012+
	pTb.LoadBitmap(IDB_VAICONS, IDB_VAICONS, IDB_VAICONS);
}

HWND GetActiveBCGMenu()
{
	CBCGPopupMenu* pop = CBCGPopupMenu::GetActiveMenu();
	if (pop)
		return pop->m_hWnd;
	return NULL;
}

BCMenu::~BCMenu()
{
	POSITION pos;
	BCMenu* pPopup;

	if ((pos = mPopups.GetHeadPosition()) != NULL)
	{
		do
		{
			pPopup = mPopups.GetAt(pos);
			delete pPopup;
		} while (mPopups.GetNext(pos) && pos);
	}
	mPopups.RemoveAll();
}

BCMenu* BCMenu::AddDynamicMenu(LPCSTR txt, int imgIdx /*= -1*/)
{
	// append to end
	BCMenu* newMenu = new BCMenu();
	newMenu->CreateMenu();
	AppendODMenu(txt, MF_POPUP, (UINT_PTR)newMenu->m_hMenu, imgIdx);
	mPopups.AddTail(newMenu);
	return newMenu;
}

BOOL BCMenu::AppendPopup(LPCTSTR lpstrText, UINT nFlags, BCMenu* popup)
{
	UINT_PTR nID = (UINT_PTR)popup->m_hMenu;
	mPopups.AddTail(popup);
	_ASSERTE(!(nFlags & MF_OWNERDRAW));
	return AppendMenu(nFlags, nID, WTString(lpstrText).c_str());
}

BOOL BCMenu::AppendODMenu(LPCTSTR lpstrText, UINT nFlags, UINT_PTR nID, int nIconNormal)
{
	_ASSERTE(!(nFlags & MF_OWNERDRAW));
	assert(!(nFlags & MF_POPUP) && "gmit: change UINT to UINT_PTR in CBCGCommandManager!!");
	GetCmdMgr()->SetCmdImage((UINT)nID, nIconNormal, FALSE);
	return AppendMenu(nFlags, nID, WTString(lpstrText).c_str());
}

BOOL BCMenu::TrackPopupMenu(const CPoint& pt, CWnd* pWnd, UINT& pick)
{
	extern HWND MainWndH;
	BOOL res = false;
	pick = 0;
	if (!gShellAttr->IsDevenv10OrHigher())
		::EnableWindow(MainWndH, FALSE);
	ReleaseCapture();
	HMENU hMenu = NULL;
	try // catch this just in case -Jer
	{
		hMenu = Detach();
		pick = GetWorkspace()->GetContextMenuManager()->TrackPopupMenu(hMenu, pt.x, pt.y, pWnd);
		if (pick)
			res = true;
		if (!gShellAttr->IsDevenv10OrHigher())
		{
			::EnableWindow(MainWndH, TRUE);
			::SetFocus(MainWndH);
			::ShowWindow(MainWndH, SW_SHOW);
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("BCM:");
		_ASSERTE(!"BCMenu::TrackPopupMenu exception caught");
	}
	if (hMenu && ::IsMenu(hMenu))
		::DestroyMenu(hMenu);
	return res;
}
