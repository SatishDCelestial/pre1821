///////////////////////////////////////////////////////////////////////////////
//
// MenuXP.h : header file
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <afxtempl.h>
#include "ArgToolTip.h"
#include "StringUtils.h"
#include "KeyBindings.h"
#include <stack>
#include "DpiCookbook/VsUIDpiHelper.h"
#include "CWndDpiAware.h"

// returns pointer to accelerator like "&a"
// but jumps over escaped &'s in format "&&"
// if 'last' is true, returns last one as DrawText considers, 
// for example in "&A&B&C&D&E" is returned position of "&E"
// else if 'last' is false, returns first found access key
// "str" must be zero terminated
template <typename CHAR_T>
const CHAR_T * FindAccessKey(const CHAR_T * str, bool last = true, bool in_binding = true)
{
	const CHAR_T * acc = nullptr;

	for (const CHAR_T * chP = str; chP && *chP; chP++)
	{
		if (!in_binding && *chP == '\t')
			break;

		if (*chP == '&' && *(chP + 1))
		{
			if (*(chP + 1) != '&')
				acc = chP;

			if (!last)
				return acc;

			chP++; // jump over next char
		}
	}

	return acc;
};

///////////////////////////////////////////////////////////////////////////////
// Menu item image management class
class CImgDesc
{
public:
    HIMAGELIST m_hImgList;
    int        m_nIndex;

    CImgDesc (HIMAGELIST hImgList = NULL, int nIndex = 0)
        : m_hImgList (hImgList), m_nIndex (nIndex)
    {
    }
};

///////////////////////////////////////////////////////////////////////////////
// Protects menu against hiding while user holds ALT key down
// - filter      - whether to perform filtering (for optional cases)
// - single_shot - if true, filtering is stopped when user presses ALT again
class CMenuXPAltRepeatFilter
{
	struct AltKeyRepeatHook * m_hook;

public:
	CMenuXPAltRepeatFilter(bool filter = true, bool single_shot = true);
	~CMenuXPAltRepeatFilter();
};

///////////////////////////////////////////////////////////////////////////////
// Menu transparency and shadow settings
class CMenuXPEffects
{
	struct WndMnu
	{
		HWND wnd;
		HMENU menu;
		int sel = -1;

		WndMnu() : wnd(), menu() {}
		WndMnu(HWND w, HMENU m) : wnd(w), menu(m) {}
	};

	BOOL bMenuFading	= FALSE;
	BOOL bMenuAnim		= FALSE;
	BOOL bMenuShadow	= FALSE;

	bool m_transparency = false;
	bool m_shadow = false;
	bool m_supported = false;

	BYTE m_AlphaCurrent;
	HWND m_AlphaWnd;
	BYTE m_MaxAlpha;
	BYTE m_MinAlpha;

 	BYTE m_FocusAlphaCurrent;
 	HWND m_FocusAlphaWnd;
	BYTE m_MaxFocusAlpha;
	BYTE m_MinFocusAlpha;

	UINT m_FadeSteps;

	CList<WndMnu> m_hWndMnuList;

	void OnWndDestroy(HWND hWnd);
	void SetBestFocusWindow();

	void WndMnu_Add(HWND w, HMENU m);
	bool WndMnu_IsEmpty();
	HWND WndMnu_TailHWND();
	HMENU WndMnu_GetHMENU(HWND wnd);
	HWND WndMnu_GetHWND(HMENU menu);
	
	bool WndMnu_RemoveByHWND(HWND wnd);

public:
	CMenuXPEffects(
		BYTE min_alpha,					// alpha when user holds CTRL pressed
		BYTE max_alpha,					// alpha when CTRL key is released
		UINT fade_time = 200,			// time to fade between min and max alpha
		bool drop_shadow = true,		// whether to drop shadow
		bool img_margin = true			// whether to show image margin
		);

	CMenuXPEffects(
		BYTE min_alpha,					// alpha of inactive menu when user holds CTRL pressed
		BYTE max_alpha,					// alpha of inactive menu when CTRL key is released
		BYTE min_alpha_focus,			// alpha of active (focused/hovered) menu when user holds CTRL pressed
		BYTE max_alpha_focus,			// alpha of active (focused/hovered) menu when CTRL key is released
		UINT fade_time = 200,			// time to fade between min and max alpha
		bool drop_shadow = true,		// whether to drop shadow
		bool img_margin = true			// whether to show image margin
		);

	~CMenuXPEffects();

	static CMenuXPEffects * sActiveEffects;

	// items accessed during rendering
	int img_width;
	int img_height;
	int img_padding;
	int text_padding;
	int text_padding_mnubr;

	// custom rendering of formatted text
	std::map<BYTE, FormatRenderer> renderers;

	// returns 'true' it 'this' is active class
	bool IsValid() const { 
		return sActiveEffects == this; 
	}

	// returns 'true' if transparency is going to be applied
	// if min_alpha <= max_alpha && min_alpha != 0xFF
	bool DoTransparency() const {
		return m_transparency;
	}

	friend class CMenuXP;
	friend class CWndMenuXP;
};

///////////////////////////////////////////////////////////////////////////////
class CMenuXP
{
// Operations
public:
    static void InitializeHook ();
    static void UninitializeHook ();

    static void SetXPLookNFeel (CWnd* pWnd, bool bXPLook = true);
	static HWND GetDpiSource(CWnd* pWnd);

    static bool GetXPLookNFeel (const CWnd* pWnd);
    static void UpdateMenuBar (CWnd* pWnd);
	static void ClearGlobals();
	static void SetXPLookNFeel(CWnd* pWnd, HMENU hMenu, bool bXPLook = true, bool bMenuBar = false);
    static bool IsOwnerDrawn (HMENU hMenu);
    static void SetMRUMenuBarItem (RECT& rc);
	static void ClearMenuItemImages();
	static void SetMenuItemImage (UINT_PTR nID, HIMAGELIST hImgList, int nIndex);
    static void OnMeasureItem (MEASUREITEMSTRUCT* pMeasureItemStruct);
    static bool OnDrawItem (DRAWITEMSTRUCT* pDrawItemStruct, HWND hWnd);
    static LRESULT OnMenuChar (HMENU hMenu, UINT nChar, UINT nFlags);
	static int GetItemHeight(HWND dpiSource, int *separator_height = NULL);
	static HMENU GetMenu(HWND hWnd);
	static HWND GetCurrentMenuWindow();	// return NULL if failed, else nonzero handle to window
	static UINT_PTR ItemFromPoint(HWND hWnd, POINT screenPt);

// Attributes
protected:
    static CMap <UINT_PTR, UINT_PTR, CStringW, CStringW&> ms_sCaptions;
    static CMap <HMENU, HMENU, CStringW, CStringW&> ms_sSubMenuCaptions;
	static CMap <UINT_PTR, UINT_PTR, FormattedTextLines, FormattedTextLines&> ms_sLines;
	static CMap <HMENU, HMENU, FormattedTextLines, FormattedTextLines&> ms_sSubMenuLines;
	static CMap <UINT_PTR, UINT_PTR, CImgDesc, CImgDesc&> ms_Images;
    static CMap <HMENU, HMENU, CImgDesc, CImgDesc&> ms_SubMenuImages;

friend class CMenuItem;
};

///////////////////////////////////////////////////////////////////////////////
#define DECLARE_MENUXP()                                                             \
    protected:                                                                       \
	afx_msg void OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu);     \
	afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct); \
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);          \
	afx_msg LRESULT OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu);

///////////////////////////////////////////////////////////////////////////////
#define ON_MENUXP_MESSAGES() \
	ON_WM_INITMENUPOPUP()    \
	ON_WM_MEASUREITEM()      \
	ON_WM_DRAWITEM()         \
	ON_WM_MENUCHAR()

///////////////////////////////////////////////////////////////////////////////
#define IMPLEMENT_MENUXP(theClass, baseClass)                                      \
    IMPLEMENT_MENUXP_(theClass, baseClass, CMenuXP::GetXPLookNFeel (this))

///////////////////////////////////////////////////////////////////////////////
#define IMPLEMENT_MENUXP_(theClass, baseClass, bFlag)                              \
    void theClass::OnInitMenuPopup (CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu) \
    {                                                                              \
	    baseClass::OnInitMenuPopup (pPopupMenu, nIndex, bSysMenu);                 \
        CMenuXP::SetXPLookNFeel (this, pPopupMenu->m_hMenu,                        \
                                 bFlag/* && !bSysMenu*/);                          \
    }                                                                              \
    void theClass::OnMeasureItem (int, LPMEASUREITEMSTRUCT lpMeasureItemStruct)    \
    {                                                                              \
        CMenuXP::OnMeasureItem (lpMeasureItemStruct);                              \
    }                                                                              \
    void theClass::OnDrawItem (int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)      \
    {                                                                              \
        if ( !CMenuXP::OnDrawItem (lpDrawItemStruct, m_hWnd) )                     \
        {                                                                          \
            baseClass::OnDrawItem (nIDCtl, lpDrawItemStruct);                      \
        }                                                                          \
    }                                                                              \
    LRESULT theClass::OnMenuChar (UINT nChar, UINT nFlags, CMenu* pMenu)           \
    {                                                                              \
        if ( CMenuXP::IsOwnerDrawn (pMenu->m_hMenu) )                              \
        {                                                                          \
            return CMenuXP::OnMenuChar (pMenu->m_hMenu, nChar, nFlags);            \
        }                                                                          \
	    return baseClass::OnMenuChar (nChar, nFlags, pMenu);                       \
    }

struct MenuItemXP
{
	CStringW Text;
	UINT Flags = MF_STRING;
	UINT Icon = 0;
	std::vector<MenuItemXP> Items;

	MenuItemXP & SetText(LPCWSTR txt)	{	Text =txt ? txt : L"";	return *this;	}
	MenuItemXP & SetFlags(UINT flags)	{	Flags = flags;			return *this;	}
	MenuItemXP & SetIcon(UINT icon)		{	Icon = icon;			return *this;	}

	MenuItemXP & Set(LPCWSTR txt, UINT flags = 0U, UINT icon = 0U)	{
		return SetText(txt).SetFlags(flags).SetIcon(icon);
	}
};

//////////////////////////////////////////////////////////////////////////
// Added by Jerry
class PopupMenuXP : public CStatic
{
	DECLARE_MENUXP()             // Into the definition of the class
	HMENU mnu;
	static PopupMenuXP		* sActiveMenu;
	static int				sActiveMenuCnt;
	static std::stack<CPoint> sReqPtStack;

	friend class PopupMenuLmb;
	friend class CWndMenuXP;

public:

	PopupMenuXP();
	~PopupMenuXP();

	struct ReqPt
	{
		ReqPt(const CPoint & pt);
		friend class PopupMenuXP;

	public:
		~ReqPt();
	};

	static std::shared_ptr<ReqPt> PushRequiredPoint(const CPoint & pt);

	void AddMenuItem(UINT_PTR id, UINT flags, LPCSTR text, UINT iconId = 0);
	void AddMenuItem(UINT_PTR id, UINT flags, const WTString& text, UINT iconId = 0) { return AddMenuItemW(id, flags, text.Wide(), iconId); }
	void AddMenuItemW(UINT_PTR id, UINT flags, LPCWSTR text, UINT iconId = 0);
	void AddSeparator(UINT_PTR id) { AddMenuItem(id, MF_SEPARATOR, ""); }

	void AddMenuItem(CMenu * parent, UINT_PTR id, UINT flags, LPCSTR text, UINT iconId = 0);
	void AddMenuItemW(CMenu * parent, UINT_PTR id, UINT flags, LPCWSTR text, UINT iconId = 0);
	void AddSeparator(CMenu * parent, UINT_PTR id) { AddMenuItem(parent, id, MF_SEPARATOR, ""); }

	BOOL AppendPopup(LPCSTR lpstrText, CMenu * popup, UINT flags = 0);
	BOOL AppendPopup(CMenu * parent, LPCSTR lpstrText, CMenu * popup, UINT flags = 0);

	BOOL AppendPopupW(LPCWSTR lpstrText, CMenu * popup, UINT flags);
	BOOL AppendPopupW(CMenu * parent, LPCWSTR lpstrText, CMenu * popup, UINT flags = 0);

	int ItemCount() const { return GetMenuItemCount(mnu); }

	INT TrackPopupMenuXP(CWnd *parent, int x, int y, UINT flags = TPM_NONOTIFY | TPM_RETURNCMD | TPM_LEFTALIGN);

	// Used within AddItems method to control the process of items creation.
	// Almost all is handled by method implementation, the minimal what
	// you should do is to make a list of id/item pairs so that you know
	// which menu item user clicked. That should be done within adding_item lambda. 
	struct AddItemsEvents
	{
		// called for each item being added to menu 
		// arg0 - points to menu item currently being added
		// arg1 - points to item identifier used to add item
		std::function<void(MenuItemXP&, UINT&, CMenu&)> adding_item;

		// called for each sub-menu (MenuItemXP with non-empty Items)
		// arg0 - points to menu item (sub-menu) currently being added
		// arg1 - points to MFC class representation of sub-menu
		std::function<void(MenuItemXP&, CMenu&)> sub_menu;
	};

	void AddItems(std::vector<MenuItemXP> & items, AddItemsEvents & events);
	void AddItems(CMenu* parent, std::vector<MenuItemXP> & items, AddItemsEvents & events);

	// CMenu compatibility functions
	void AppendMenu(UINT flags, UINT_PTR id, const char *text) { AddMenuItem(id, flags, text); }
	int TrackPopupMenu(UINT flags, int x, int y, CWnd *parent) { return TrackPopupMenuXP(parent, x, y, flags); }

	static bool IsMenuActive() { return sActiveMenu != NULL; }
	static int ActiveMenuCount() { return sActiveMenuCnt; }
	static void CancelActiveMenu();
protected:
	DECLARE_MESSAGE_MAP()
private:
	typedef std::list<CMenu*> MenuPopupList;
	MenuPopupList		mPopups;
};

class MenuItemLmb
{
public:
	typedef std::function<void(void)> Cmd;
	typedef std::function<bool(MenuItemLmb&)> AddingFnc;

	CStringW Text;
	UINT Icon;
	UINT Flags;
	MenuItemLmb * Parent;
	bool HLSyntax;
	Cmd Command;	 // command to be executed when user selects this item
	Cmd HotCommand;	 // command to be executed when item is highlighted
	AddingFnc OnAdd; // called before UI item is created (can be used to set current state of item or return false to avoid item adding)

	MenuItemLmb(LPCWSTR text, UINT flags, UINT icon, AddingFnc on_add, Cmd cmd, Cmd hot_cmd)
		: Text(text), Icon(icon), Flags(flags), Parent(nullptr), HLSyntax(false), Command(cmd), HotCommand(hot_cmd), OnAdd(on_add)
	{
	}

	MenuItemLmb(LPCWSTR text, UINT flags, UINT icon, bool enabled, Cmd cmd, Cmd hot_cmd)
		: MenuItemLmb(text, flags, icon, AddingFnc(), cmd, hot_cmd) {
		if (!enabled)
			Flags|=(MF_GRAYED |MF_DISABLED);
	}

	MenuItemLmb(LPCWSTR text, AddingFnc on_add, Cmd cmd, Cmd hot_cmd)					: MenuItemLmb(text, MF_STRING, 0, on_add, cmd, hot_cmd) {}
	MenuItemLmb(const CStringW& text, AddingFnc on_add, Cmd cmd, Cmd hot_cmd)           : MenuItemLmb((LPCWSTR)text, MF_STRING, 0, on_add, cmd, hot_cmd) {}
	MenuItemLmb(const WTString& text, AddingFnc on_add, Cmd cmd, Cmd hot_cmd)			: MenuItemLmb(text.Wide(), MF_STRING, 0, on_add, cmd, hot_cmd) {}
	MenuItemLmb(LPCWSTR text, bool enabled, Cmd cmd, Cmd hot_cmd)						: MenuItemLmb(text, MF_STRING, 0, enabled, cmd, hot_cmd) {}
	MenuItemLmb(const CStringW& text, bool enabled, Cmd cmd, Cmd hot_cmd)               : MenuItemLmb((LPCWSTR)text, MF_STRING, 0, enabled, cmd, hot_cmd) {}
	MenuItemLmb(const WTString& text, bool enabled, Cmd cmd, Cmd hot_cmd)				: MenuItemLmb(text.Wide(), MF_STRING, 0, enabled, cmd, hot_cmd) {}
	MenuItemLmb(LPCWSTR text, Cmd cmd, Cmd hot_cmd)										: MenuItemLmb(text, true, cmd, hot_cmd) {}
	MenuItemLmb(const CStringW& text, Cmd cmd, Cmd hot_cmd)								: MenuItemLmb((LPCWSTR)text, true, cmd, hot_cmd) {}
	MenuItemLmb(const WTString& text, Cmd cmd, Cmd hot_cmd)								: MenuItemLmb(text.Wide(), true, cmd, hot_cmd) {}
	MenuItemLmb(LPCWSTR text, bool enabled)												: MenuItemLmb(text, enabled, Cmd(), Cmd()) {}
	MenuItemLmb(const CStringW& text, bool enabled)										: MenuItemLmb((LPCWSTR)text, enabled, Cmd(), Cmd()) {}
	MenuItemLmb(const WTString& text, bool enabled)										: MenuItemLmb(text.Wide(), enabled, Cmd(), Cmd()) {}
	MenuItemLmb(UINT flags = MF_SEPARATOR)												: MenuItemLmb(L"", flags, 0, true, Cmd(), Cmd()) {}

	MenuItemLmb & SetFlags(UINT flags)				{ Flags = flags;			return *this; }
	MenuItemLmb & SetIcon(UINT icon)				{ Icon = icon;				return *this; }
	MenuItemLmb & SetCommand(Cmd cmd)				{ Command = cmd;			return *this; }
	MenuItemLmb & SetHotCommand(Cmd cmd)			{ HotCommand = cmd;			return *this; }
	MenuItemLmb & SetOnAdd(AddingFnc on_add)		{ OnAdd = on_add;			return *this; }
	MenuItemLmb & SetParent(MenuItemLmb * parent)	{ Parent = parent;			return *this; }

	MenuItemLmb & SetText(LPCWSTR txt, bool preserve_key_binding = false);

	MenuItemLmb & SetKeyBinding(LPCWSTR binding);
	MenuItemLmb & SetKeyBindingForCommand(LPCSTR cmd, LPCSTR vc6binding = nullptr);
	CStringW GetKeyBinging() const;

	MenuItemLmb & Set(LPCWSTR txt, UINT flags = 0U, UINT icon = 0U){
		return SetText(txt).SetFlags(flags).SetIcon(icon);
	}

	wchar_t GetAccessKey(bool in_binding = false) const;
	void RemoveAccessKey();
	bool SetAccessKey(int index);
	bool IsSeparator() const	{ return (Flags & MF_SEPARATOR) == MF_SEPARATOR; }
	bool IsEnabled() const		{ return (Flags & (MF_GRAYED | MF_DISABLED)) == 0; }

	CStringW ComparableText(bool preserveBinding = false, bool preserveAccessKey = false);

	bool HasUniqueAccessKeys();
	bool GenerateUniqueAccessKeys(bool preserveUniqueExisting = true);
	void GenerateAlnumAccessKeys(bool preserveExisting = true, LPCWSTR bindSepar = L"  ");
	void AddSeparator(bool force = false);
	void NormalizeSeparators();

	std::vector<MenuItemLmb> Items;
};

class PopupMenuLmb
{
	friend class CMenuXP;
	friend class CWndMenuXP;

public:
	class Events
	{
	public:
		std::function<void(PopupMenuXP&)> InitMenu;

		std::function<bool(MenuItemLmb&, CMenu&)> AddingItem;
		std::function<void(MenuItemLmb&, CMenu&)> AddingPopup;

		std::function<bool(PopupMenuXP&)> Opening;
		std::function<void(PopupMenuXP&, INT)> Closed;
		std::function<bool(MenuItemLmb&)> ItemSelected;
		std::function<bool(MenuItemLmb&)> ItemHot;

		std::function<bool(LRESULT&, HWND,UINT,WPARAM,LPARAM)> WndMessage;

		bool OnWindowMessage(LRESULT & rslt, HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			if (WndMessage)
				return WndMessage(rslt, wnd, msg, wParam, lParam);

			return false;
		}

		Events()
		{
			ItemSelected = [](MenuItemLmb& mi)
			{
				if (mi.Command)
				{
					mi.Command();
					return true;
				}
				return false;
			};

			ItemHot = [](MenuItemLmb& mi)
			{
				if (mi.HotCommand)
				{
					mi.HotCommand();
					return true;
				}
				return false;
			};
		}
	};

	PopupMenuLmb() = default;

	std::vector<MenuItemLmb> Items; // These are only item templates !!!

	bool Show(CWnd *parent, int x, int y, Events *events = nullptr);
	void UpdateSelection(HMENU mnu, UINT_PTR item);

	// adds separator if list is not empty and last item is not separator
	void AddSeparator(bool force = false);

	// - removes separator if it is first or last item in list
	// - preserves only single separator in case of multiple in a row
	void NormalizeSeparators();

	bool HasUniqueAccessKeys();
	bool GenerateUniqueAccessKeys(bool preserveUniqueExisting = true);
	void GenerateAlnumAccessKeys(bool preserveExisting = true, LPCWSTR bindSepar = L"  ");

	// Maps virtual keys in received WM_CHAR messages to chars 
	// that window would get with English layout on keyboard.
	// This is useful when menu uses numeric access keys.
	bool MapVirtualKeysToInvariantChars = false;

private:
	static PopupMenuLmb * sActiveInstance;
	static Events * sActiveEvents;
	static std::vector<MenuItemLmb> * sActiveItems; // do not confuse with PopupMenuLmb::Items
};

class MenuXpHook
{
public:
	MenuXpHook(CWnd * pWnd);
	~MenuXpHook();
};

///////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.
