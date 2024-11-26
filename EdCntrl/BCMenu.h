#ifndef BCMenu_H
#define BCMenu_H

#include <afxtempl.h>
#include <afxcoll.h>

void LoadBcgBitmap();
extern HWND GetActiveBCGMenu();

class BCMenu : public CMenu
{
	typedef CTypedPtrList<CPtrList, BCMenu*> MenuPopupList;
	MenuPopupList mPopups;

  public:
	BCMenu() : CMenu()
	{
	}
	~BCMenu();

	BOOL AppendSeparator(DWORD extraStyle = 0)
	{
		return AppendODMenu(_T(""), MF_SEPARATOR | extraStyle);
	}
	BOOL AppendODMenu(LPCTSTR lpstrText, UINT nFlags, UINT_PTR nID = 0, int nIconNormal = -1);
	BOOL AppendPopup(LPCTSTR lpstrText, UINT nFlags, BCMenu* popup);
	BOOL TrackPopupMenu(const CPoint& pt, CWnd* pWnd, UINT& pick);
	BCMenu* AddDynamicMenu(LPCSTR txt, int imgIdx = -1);
};

#endif
