// VACompletionBox.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "VACompletionBox.h"
#include "VACompletionSet.h"
#include "Edcnt.h"
#include "expansion.h"
#include "FontSettings.h"
#include "Settings.h"
#include "MenuXP/MenuXP.h"
#include "MenuXP/ToolbarXP.h"
#include "FileTypes.h"
#include "project.h"
#include "DevShellAttributes.h"
#include "VASeException/VASeException.h"
#include "HookCode.h"
#include "focus.h"
#include "SyntaxColoring.h"
#include "IdeSettings.h"
#include "WindowUtils.h"
#include "ImageListManager.h"
#include "DoubleBuffer.h"
#include "StringUtils.h"
#include "TextOutDC.h"
#include "MenuXP/Draw.h"
#include "DpiCookbook/VsUIDpiHelper.h"
#include "WtException.h"
#include "VAParse.h"
#include "ToolTipEditCombo.h"
#include "VAThemeUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern const uint WM_RETURN_FOCUS_TO_EDITOR;

class PatchScrollWindowEx
{
  public:
	PatchScrollWindowEx() : patched(false)
	{
		assert(gShellAttr);
		if (!gShellAttr || !gShellAttr->RequiresWin32ApiPatching())
			return;
		patched = !!::WtHookCode(GetProcAddress(GetModuleHandle("user32.dll"), "ScrollWindowEx"),
		                         myScrollWindowEx, (void**)&origScrollWindowEx, 0);
	}
	~PatchScrollWindowEx()
	{
		if (patched)
			::WtUnhookCode(myScrollWindowEx, (void**) & origScrollWindowEx);
	}

  protected:
	bool patched;

	static int(WINAPI* origScrollWindowEx)(HWND hWnd, int dx, int dy, CONST RECT* prcScroll, CONST RECT* prcClip,
	                                       HRGN hrgnUpdate, LPRECT prcUpdate, UINT flags);
	static int WINAPI myScrollWindowEx(HWND hwnd, int dx, int dy, CONST RECT* prcScroll, CONST RECT* prcClip,
	                                   HRGN hrgnUpdate, LPRECT prcUpdate, UINT flags)
	{
		if (hwnd && ::IsWindow(hwnd) && myGetProp(hwnd, "__VA_do_not_scrollwindowex"))
		{
			CRect rect;
			::GetClientRect(hwnd, rect);
			if (prcScroll)
				rect &= *prcScroll;
			if (prcClip)
				rect &= *prcClip;
			::InvalidateRect(hwnd, rect, !!(flags & SW_ERASE));
			if (hrgnUpdate)
				::SetRectRgn(hrgnUpdate, rect.left, rect.top, rect.right, rect.bottom);
			if (prcUpdate)
				*prcUpdate = rect;
			return SIMPLEREGION;
		}

		return origScrollWindowEx(hwnd, dx, dy, prcScroll, prcClip, hrgnUpdate, prcUpdate, flags);
	}
};

int(WINAPI* PatchScrollWindowEx::origScrollWindowEx)(HWND hWnd, int dx, int dy, CONST RECT* prcScroll,
                                                     CONST RECT* prcClip, HRGN hrgnUpdate, LPRECT prcUpdate,
                                                     UINT flags) = NULL;
std::unique_ptr<PatchScrollWindowEx> pswe;

void InitScrollWindowExPatch()
{
	if (!pswe)
		pswe = std::make_unique<PatchScrollWindowEx>();
}

void UninitScrollWindowExPatch()
{
	if (pswe)
		pswe.reset();
}

/////////////////////////////////////////////////////////////////////////////
// VACompletionBox

VACompletionBox::VACompletionBox() : mDblBuff(new CDoubleBuffer(m_hWnd))
{
	if (UseVsTheme())
		InitScrollWindowExPatch();

	m_compSet = NULL;
	m_tooltip = NULL;
	m_font_type = VaFontType::ExpansionFont;

	enable_themes_beyond_vs2010 = !!Psettings->m_alwaysUseThemedCompletionBoxSelection || gShellAttr->IsCppBuilder();
}

bool VACompletionBox::UpdateFonts(VaFontTypeFlags changed, UINT dpi /*= 0*/)
{
	if (!IsFontAffected(changed, m_font_type))
		return false;

	return __super::UpdateFonts(changed, dpi) ||
	       SUCCEEDED(m_boldFont.Update(dpi ? dpi : GetDpiY(), m_font_type, FS_Bold));
}

int GetTypeImgIdx(uint type, uint symAttributes, bool icon_for_completion_box)
{
	// listbox imagelist might have additional images at list head.  Use offset
	// as required so that this function can be called from both listboxes and
	// windows that don't have the expanded list.
	const int offset = icon_for_completion_box && g_CompletionSet ? g_ExImgCount : 0;
	if (IS_VSNET_IMG_TYPE(type))
		return TYPE_TO_IMG_IDX(type);
	if (icon_for_completion_box && IS_IMG_TYPE(type))
		return TYPE_TO_IMG_IDX(type) + offset;

	// 	if((s_popType==ET_EXPAND_VSNET || s_popType==ET_EXPAND_TEXT) && type < g_ExImgCount)
	// 		return type;
	if (type == ET_AUTOTEXT || type == ET_AUTOTEXT_TYPE_SUGGESTION)
		return ICONIDX_VATEMPLATE + offset;
	if (type == ET_VS_SNIPPET)
		return ICONIDX_VS11_SNIPPET + offset;
	if (type == ET_SUGGEST)
		return ICONIDX_SUGGESTION + offset;
	if (type == ET_SUGGEST_BITS)
		return ICONIDX_SUGGESTION + offset;

	if (icon_for_completion_box && (g_CompletionSet->m_popType == ET_EXPAND_VSNET) &&
	    ((int)type >= 0 && (int)type < g_ExImgCount))
	{
		// p4 change 9104, 9113
		return (int)type; // fixes icons for ::<ctrl_space>, where the type is the same as the img
	}

	int attr = 0;
	int imgIdx = 6;
	if (symAttributes & V_PRIVATE)
		attr = 4;
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	else if (symAttributes & V_PUBLISHED)
		attr = 0; // [case: 135860] same as public
#else
	else if (symAttributes & V_INTERNAL)
		attr = 4;
#endif
	else if (symAttributes & V_PROTECTED)
		attr = 3;

	_ASSERTE((type & TYPEMASK) == type || (type & ET_MASK) == ET_MASK_RESULT);
	if (type == CLASS)
		imgIdx = 0;
	else if (type == CONSTANT)
		imgIdx = 1;
	else if (type == DELEGATE)
		imgIdx = 2;
	else if (type == C_ENUM)
		imgIdx = 3;
	else if (type == C_ENUMITEM)
		imgIdx = 4;
	else if (type == EVENT)
		imgIdx = 5;
	else if (type == VAR || type == LINQ_VAR || type == Lambda_Type)
		imgIdx = 7;
	else if (type == C_INTERFACE)
		imgIdx = 8;
	else if (type == DEFINE)
		imgIdx = 9;
	else if (type == MAP)
		imgIdx = 10;
	else if (type == MAPITEM)
		imgIdx = 11;
	else if (type == FUNC)
		imgIdx = 12;
	else if (type == GOTODEF)
		imgIdx = 12;
	else if (type == COMMENT) // goto method
		imgIdx = 12;
	else if (type == MODULE)
		imgIdx = 14;
	else if (type == NAMESPACE)
		imgIdx = 15;
	else if (type == OPERATOR)
		imgIdx = 16;
	else if (type == PROPERTY)
		imgIdx = 17;
	else if (type == STRUCT)
		imgIdx = 18;
	else if (type == TEMPLATE || type == TEMPLATE_DEDUCTION_GUIDE)
		imgIdx = 19;
	else if (type == TYPE || type == TEMPLATETYPE)
		imgIdx = 20;
	else if (type == RESWORD)
		return ICONIDX_RESWORD + offset;
	else if (type == ET_SCOPE_SUGGESTION)
	{
		if (gShellAttr->IsDevenv11OrHigher())
			return ICONIDX_SCOPE_SUGGEST_V11 + offset; // same as old ICONIDX_RESWORD
		return ICONIDX_RESWORD + offset;
	}
	else
	{
		if (gShellAttr->IsDevenv11OrHigher())
			return ICONIDX_SCOPE_SUGGEST_V11 + offset; // same as old ICONIDX_RESWORD
		return ICONIDX_RESWORD + offset;
	}

	imgIdx = (imgIdx * 6) + attr + g_IconIdx_VaOffset;
	if (Is_HTML_JS_VBS_File(gTypingDevLang))
	{
// HACK: below returns hard coded indexes into their image list for Functions or VAR's
#define JS_IMGLIST_IDX_VAR 0x8
#define JS_IMGLIST_IDX_FUNC 0x0
		if ((g_ExImgCount < imgIdx) && g_CompletionSet && icon_for_completion_box &&
		    (g_CompletionSet->m_popType == ET_EXPAND_VSNET))
			return (type == FUNC) ? JS_IMGLIST_IDX_FUNC : JS_IMGLIST_IDX_VAR;
	}

	return imgIdx + offset;
}

VACompletionBox::~VACompletionBox()
{
	if (m_tooltip)
	{
		if (m_tooltip->GetSafeHwnd())
			m_tooltip->DestroyWindow();
		delete m_tooltip;
	}
	delete mDblBuff;
}

static LPCSTR _NETTipsAttrs[] = {"public ", "internal ", "friend ", "protected ", "private ", "shortcut ", NULL};

static INT _VATypes[] = {CLASS,  //	"classes",//0
                         DEFINE, //	"constants",
                         TYPE,   //	"delegates",
                         TYPE,   //	"enums",
                         DEFINE, //	"enum items",
                         TYPE,   //	"events",//5
                         TYPE,   //	"", // <white square with three black lines>",
                         VAR,    //	"variables",
                         CLASS,  //	"interfaces",
                         DEFINE, //	"macros",
                         TYPE,   //	"maps",//10
                         VAR,    //	"map items",
                         FUNC,   //	"methods",
                         TYPE,   //	"", // <two red, 3-d rectangles, stacked>",x
                         TYPE,   //	"modules",
                         TYPE,   //	"namespaces",//15
                         FUNC,   //	"operators",
                         VAR,    //	"properties",
                         CLASS,  //	"structs",
                         TYPE,   //	"templates",
                         TYPE,   //	"typedefs",//20
                         NULL};

// http://forums.microsoft.com/MSDN/ShowPost.aspx?PostID=953123&SiteID=1
static LPCSTR _NETTipsTypes[] = {"classes", // 0
                                 "constants",
                                 "delegates",
                                 "enums",
                                 "enum items",
                                 "events", // 5
                                 "exceptions",
                                 "variables",
                                 "interfaces",
                                 "macros",
                                 "maps", // 10
                                 "map items",
                                 "methods",
                                 "external declarations", // overloads per the forums
                                 "modules",
                                 "namespaces", // 15
                                 "operators",
                                 "properties",
                                 "structs",
                                 "templates",
                                 "typedefs", // 20
                                 "types",
                                 "unions",
                                 "variables",  // variables per the forums
                                 "globals",    // valuetypes per the forums
                                 "intrinsics", // 25
                                 "",           // java method?
                                 "",           // java variable?
                                 "",           // java struct?
                                 "",           // java namespace?
                                 "",           // java interface?	//30
                                 NULL};

CString GetVSNETFilterTips(int i)
{
	static const CString kFilterPrefix = "Display only ";
	static const CString kDefaultFilterTip = "Filter by type...";

	if (i == (ICONIDX_VATEMPLATE + g_ExImgCount) || i == ET_SUGGEST_BITS)
		return kFilterPrefix + "VA Snippets";
	if (i == VSNET_SNIPPET_IDX)
		return kFilterPrefix + "VS Snippets";

	if (!gShellAttr->IsDevenv14OrHigher() && gShellAttr->IsDevenv10OrHigher())
	{
		// [case: 36993] Changed filter toolbar tip text to be "Filter by..."
		// [case: 44530] but only in Vs2010
		// [case: 164000] Fix filter toolbar tip text for VS2015 and higher, leave behavior for VS2010, VS2012, VS2013 
		return kDefaultFilterTip;
	}

	if (i == ET_VS_SNIPPET)
		return kFilterPrefix + "VS Code Snippets";

	if (i == ET_AUTOTEXT || i == (ICONIDX_VATEMPLATE /*|VA_TB_CMD_FLG*/)) // we treat ICONIDX_VATEMPLATE as a filter,
	                                                                      // not a command to edit templates
		return kFilterPrefix + "VA Snippets";

	int imgIdx;
	if (IS_IMG_TYPE(i))
		imgIdx = TYPE_TO_IMG_IDX(i);
	else
	{
		imgIdx = i;
		if (g_CompletionSet->m_popType != ET_EXPAND_VSNET && imgIdx > g_IconIdx_VaOffset)
			imgIdx -= g_IconIdx_VaOffset; // Since we prepended our tips
	}

	int type;
	int attr;
	if (g_ExImgCount == 90)
	{
		type = imgIdx / 3;
		attr = imgIdx % 3;
		if (type == 22)
			type = 20; // typedef
		if (type == 23)
			type = 7; // variables
		if (attr)
			attr += 2;
	}
	else
	{
		type = imgIdx / 6;
		attr = imgIdx % 6;
	}

	if (i == VSNET_RESERVED_WORD_IDX)
		return kFilterPrefix + "reserved words";
	if (type < 31 && *_NETTipsTypes[type] && attr < 6)
		return kFilterPrefix + CString(_NETTipsAttrs[attr]) + _NETTipsTypes[type];

	if (g_ExImgCount != 164 && g_ExImgCount != 180 && g_ExImgCount != 253 && // VS2005 Nov C++
	    g_ExImgCount != 220 &&                                               // VS2005 Release C#
	    g_ExImgCount != 226 &&                                               // VS2008 Beta 1 C#
	    g_ExImgCount != 236                                                  // VS2012 C#
	)
	{
#ifdef _DEBUG
		static bool once = true;
		if (once)
		{
			CString msg;
			CString__FormatA(msg, "unknown value for g_ExImgCount: %d", g_ExImgCount);
			ErrorBox(msg);
			once = false;
		}
#endif // _DEBUG
	}

#ifdef _DEBUG
	CString tipText;
	CString__FormatA(tipText, "Unknown param passed to GetVSNETFilterTips: i(0x%x) imgIdx(%d) type(%d)", i, imgIdx,
	                 type);
	return tipText;
#else
	return kDefaultFilterTip;
#endif // _DEBUG
}

CString GetFileCompletionFilterTips(int i)
{
	int imgIdx;
	if (IS_IMG_TYPE(i))
		imgIdx = TYPE_TO_IMG_IDX(i);
	else
	{
		imgIdx = i;
		if (g_CompletionSet->m_popType != ET_EXPAND_VSNET && imgIdx > g_IconIdx_VaOffset)
			imgIdx -= g_IconIdx_VaOffset; // Since we prepended our tips
	}

	switch (imgIdx)
	{
	case 0x22:
		return CString("Directories");
	case 0x24:
		return CString("Header files");
	case 0x27:
		return CString("Interface definition files");
	default:
		return CString();
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VACompletionBox, CListCtrl2008)
//{{AFX_MSG_MAP(VACompletionBox)
ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnGetdispinfo)
ON_WM_TIMER()
//}}AFX_MSG_MAP
ON_WM_VSCROLL()
ON_WM_SIZING()
ON_WM_SIZE()
ON_REGISTERED_MESSAGE(WM_RETURN_FOCUS_TO_EDITOR, OnReturnFocusToEditor)
END_MESSAGE_MAP()
#pragma warning(pop)

// #include "VSNetIconDefs.h"
/////////////////////////////////////////////////////////////////////////////
// VACompletionBox message handlers
LRESULT VACompletionBox::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	try // Added to catch bogus m_ed issue with Resharper: case=25981
	{
		if (message == WM_KEYDOWN)
		{
			// [case: 94723] reduce timer for vs2015 C# async tip
			const uint timerTime = gShellAttr && gShellAttr->IsDevenv14OrHigher() && gTypingDevLang == CS ? 500u : 600u;
			SetTimer(Timer_UpdateTooltip, timerTime, NULL);
			if (m_tooltip && m_tooltip->GetSafeHwnd())
				m_tooltip->ShowWindow(SW_HIDE);
		}
		if (message == WM_KEYDOWN && VAGetFocus() == GetSafeHwnd())
		{
			m_compSet->m_ed->vSetFocus();
			m_compSet->m_ed->PostMessage(message, wParam, lParam);

			return FALSE;
		}

		LRESULT r = CListCtrl2008::WindowProc(message, wParam, lParam);

		if (message == WM_SHOWWINDOW && m_tooltip && m_tooltip->GetSafeHwnd())
		{
			if (wParam == SW_HIDE)
			{
				m_tooltip->ShowWindow(SW_HIDE);
			}
		}
		if (message == WM_LBUTTONDOWN)
		{
			SetTimer(Timer_UpdateTooltip, 50, NULL);
			if (g_currentEdCnt)
				g_currentEdCnt->m_typing = FALSE;
			m_compSet->m_ed->vSetFocus();
		}
		if ((message == WM_LBUTTONUP && !gShellAttr->IsDevenv11OrHigher()) ||
		    (message == WM_LBUTTONDBLCLK && gShellAttr->IsDevenv11OrHigher()))
		{
			// [case: 68256]
			m_compSet->ExpandCurrentSel();
		}
		//	if(message == WM_COMMAND)
		//	{
		//		INT I = 123;
		//	}
		//	if(message == WM_COMMNOTIFY)
		//	{
		//		INT I = 123;
		//	}
		if (message == WM_NOTIFY)
		{
			NMHDR* pNMHDR = (NMHDR*)lParam;
			NMTOOLBAR* pObj = (NMTOOLBAR*)lParam;
			if (pNMHDR->code == TBN_HOTITEMCHANGE)
			{
				m_compSet->m_ed->vSetFocus();
				if (m_tooltip && m_tooltip->GetSafeHwnd())
					m_tooltip->ShowWindow(SW_HIDE);
			}
			if (pNMHDR->code == TBN_GETINFOTIP)
			{
				NMTBGETINFOTIP* inf = (NMTBGETINFOTIP*)lParam;
				TBBUTTONINFO bu;
				ZeroMemory(&bu, sizeof(bu));
				bu.cbSize = sizeof(bu);
				bu.dwMask = TBIF_IMAGE | TBIF_COMMAND;
				m_compSet->m_tBar->GetToolBarCtrl().GetButtonInfo(inf->iItem, &bu);
				CToolTipCtrl* tip = m_compSet->m_tBar->GetToolBarCtrl().GetToolTips();
				if (tip)
					tip->ModifyStyleEx(0, WS_EX_TOPMOST, 0);

				if (bu.idCommand == (ICONIDX_EXPANSION | VA_TB_CMD_FLG))
				{
					if (Psettings->m_bUseDefaultIntellisense && gShellAttr && gShellAttr->IsDevenv11OrHigher())
					{
						WTString listMemberBindingTip = GetBindingTip("Edit.ListMembers");
						_tcscpy(inf->pszText, ("List Members or Completions" + listMemberBindingTip).c_str());
					}
					else
						_tcscpy(inf->pszText, "Show all symbols (Ctrl+Space)");
				}
				else if (bu.idCommand == (ICONIDX_MODFILE | VA_TB_CMD_FLG))
					_tcscpy(inf->pszText, "Edit VA Snippet...");
				else if (bu.idCommand == (ICONIDX_RESWORD | VA_TB_CMD_FLG))
					_tcscpy(inf->pszText, "Reserved words");
				else if (bu.idCommand == (ICONIDX_SHOW_NONINHERITED_ITEMS_FIRST | VA_TB_CMD_FLG))
					_tcscpy(inf->pszText, "Show non-inherited first");
				else if (bu.idCommand == ET_SUGGEST_BITS || bu.idCommand == (ICONIDX_SUGGESTION | VA_TB_CMD_FLG))
					_tcscpy(inf->pszText, "Suggestions from surrounding code");
				else if (m_compSet->IsFileCompletion())
					_tcscpy(inf->pszText, (LPCSTR)GetFileCompletionFilterTips(bu.idCommand));
				else
					_tcscpy(inf->pszText, (LPCSTR)GetVSNETFilterTips(bu.idCommand));
			}
			if (pNMHDR->code == TBN_ENDDRAG)
			{
				m_compSet->FilterListType(pObj->iItem);
				m_compSet->m_tBar->Invalidate(FALSE);
			}
		}
		if (message == WM_MOUSEMOVE /*&& s_popType != ET_SUGGEST*/ && Psettings->m_bDisplayFilteringToolbar)
		{
			m_compSet->ShowFilteringToolbar(true, true);
		}
		if (message == WM_SHOWWINDOW && wParam == SW_HIDE)
		{
			if (m_tooltip && m_tooltip->GetSafeHwnd())
			{
				m_tooltip->ShowWindow(SW_HIDE);
			}
			m_compSet->ShowFilteringToolbar(false);
		}

		switch (message)
		{
		case WM_ERASEBKGND:
		case WM_NCPAINT:
		case WM_PAINT:
		case WM_NCACTIVATE:
		case WM_PRINT:
		case WM_PRINTCLIENT:
			if (UseVsTheme())
			{
				// draw border in VS2010 menu border colour
				// note: during WM_NCACTIVATE, border is drawn directly (without paint messages), so, we need to redraw
				// it to prevent flashing
				CWindowDC dc(this);
				CSaveDC saved(dc);
				CPen pen;
				if (gShellAttr->IsDevenv12OrHigher())
					pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
					              g_IdeSettings->GetEnvironmentColor(L"CommandBarBorder", FALSE));
				else if (gShellAttr->IsDevenv11())
					pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
					              GetVS2010Colour(VSCOLOR_COMBOBOX_MOUSEOVER_BORDER));
				else // if (gShellAttr->IsDevenv10())
					pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1),
					              GetVS2010Colour(VSCOLOR_COMMANDBAR_MENU_BORDER));
				dc.SelectObject(pen);
				dc.SetBkMode(TRANSPARENT);
				CRect rect;
				GetWindowRect(rect);
				rect.MoveToXY(0, 0);
				rect.DeflateRect(0, 0, 1, 1);
				CPoint pts[] = {rect.TopLeft(), CPoint(rect.right, rect.top), rect.BottomRight(),
				                CPoint(rect.left, rect.bottom), rect.TopLeft()};
				dc.Polyline(pts, countof(pts));
				dc.GetPixel(0, 0); // force flush to avoid flicker
			}
			break;
		}

		return r;
	}
	catch (...)
	{
		VALOGEXCEPTION("SACS:");
		return 0;
	}
}

void VACompletionBox::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = -1;

	//	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
	//	LV_ITEM* pItem= &(pDispInfo)->item;
	//	if (pItem && pItem->mask & LVIF_TEXT)
	//	{
	//		CString text;
	//		long image;
	//		m_compSet->GetDisplayText(pItem->iItem, text, &image);
	//		strcpy(pItem->pszText, text);
	//		pItem->iImage = image;
	//	}
	//
	//	*pResult = 0;
}

BOOL VACompletionBox::Create(CWnd* pParentWnd)
{
	CRect rc(0, 0, 100, 100);
	if (!CreateEx(WS_EX_NOPARENTNOTIFY | WS_EX_TOOLWINDOW | WS_EX_LEFT,
	              WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_POPUP | WS_DLGFRAME | LVS_SHOWSELALWAYS |
	                  LVS_NOCOLUMNHEADER | LVS_REPORT | LVS_SHAREIMAGELISTS | LVS_SINGLESEL | LVS_OWNERDRAWFIXED |
	                  LVS_OWNERDATA,
	              rc, pParentWnd, 0))
	{
		return FALSE;
	}

	::VsScrollbarTheme(m_hWnd);

	if (gShellAttr->IsDevenv10OrHigher())
	{
		// WS_EX_CLIENTEDGE leaves a bold edge on the left that is
		// more noticeable than the soft edge on the bottom and right
		// with WS_EX_STATICEDGE.
		ModifyStyle(WS_THICKFRAME, WS_BORDER);
	}
	InsertColumn(0, "", LVCFMT_LEFT, 60);
	SendMessage(WM_NCACTIVATE, TRUE);

	// gmit: Since we have gradient in the background, one item has different appearance depending on its vertical
	// position.
	//       ListCtrl internaly uses ScrollWindowEx for scrolling with mouse wheel and by keyboard arrows, which render
	//       our items with wrong background colour. We patch ScrollWindowEx and, instead of scrolling, we invalidate
	//       affected area.
	if (UseVsTheme())
		mySetProp(m_hWnd, "__VA_do_not_scrollwindowex", (HANDLE)1);

	//////////////////////////////////////////////////////////////////////////
	return TRUE;
	//	return CListCtrl2008::Create(lpszClassName, lpszWindowName, dwStyle, rect, pParentWnd, nID, pContext);
}

COLORREF WTColorFromType(int type)
{
	if ((type & ET_MASK) == ET_MASK_RESULT)
		return 0xdead;
	if (IS_IMG_TYPE(type))
	{
		if (type == IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_RENAME))
			return 0xdead; // [case: 95401]

		type = TYPE_TO_IMG_IDX(type);
		int i = type; // icon index
		type = i / 6;
		if (g_ExImgCount == 90)
		{ // Hack for VC++ icons
			type = i / 3;
			if (type == 22)
				type = 20; // typedef
			if (type == 23)
				type = 7; // variables
		}
		// 		else if(g_ExImgCount != ICONIDX_VAOFSET && g_ExImgCount != 164 && g_ExImgCount != 180 && g_ExImgCount !=
		// 220)
		// 		{
		// 			return 0xdead;
		// 		}
		if (type > 21 || !_VATypes[type])
			return 0xdead;
		type = _VATypes[type];
	}

	_ASSERTE((type & TYPEMASK) == type);
	switch (type)
	{
	case C_ENUMITEM:
	case DEFINE:
		return Psettings->m_colors[C_Macro].c_fg;
	case VAR:
		return Psettings->m_colors[C_Var].c_fg;
	case FUNC:
	case GOTODEF: // added for methods in locally defined classes (case 2001)
		return Psettings->m_colors[C_Function].c_fg;
	case CLASS: // added for local typedefs
	case TYPE:
	case STRUCT:
	case C_ENUM:
	case NAMESPACE:
	case MODULE:
	case MAP:
	case C_INTERFACE:
		return Psettings->m_colors[C_Type].c_fg;
	case RESWORD:
		return Psettings->m_colors[C_Type].c_fg;
	}

	return 0xdead;
}

#define ICON_SPACING (VsUI::DpiHelper::LogicalToDeviceUnitsX(4))
#define ICON_WIDTH (VsUI::DpiHelper::ImgLogicalToDeviceUnitsX(16))
extern UINT g_baseExpScope;
void VACompletionBox::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	int nItem = (int)lpDrawItemStruct->itemID;
	CRect rect(0, 0, 0, 0);
	GetItemRect(nItem, &rect, LVIR_BOUNDS);

	if (UseVsTheme() && !rect.IsRectEmpty())
	{
		if (gShellAttr->IsDevenv11OrHigher())
		{
			CRect clRc;
			GetClientRect(clRc);
			if (clRc.Width() > (rect.Width() + 2))
			{
				// [case: 70878]
				// flicker at right side of items where scrollbar had been.
				// in dark theme when scrollbar is removed, there is often a flash
				// of white due to GetItemRect not returning correct bounds.
				// likewise after resize of width to smaller box when scrollbar
				// is present - flash of white occurs before scrollbar is drawn.
				rect.right = clRc.right;
			}
		}

		try
		{
			CDoubleBufferedDC dc(*mDblBuff, *pDC,
			                     gShellAttr->IsDevenv11OrHigher() ? g_IdeSettings->GetEnvironmentColor(L"Window", false)
			                                                      : ::GetSysColor(COLOR_WINDOW),
			                     &rect);
			DrawItem(&dc, nItem, &rect);
		}
		catch (const WtException& e)
		{
			UNUSED_ALWAYS(e);
			VALOGERROR("ERROR: VACompletionBox DrawItem exception caught 1");
		}
	}
	else
		DrawItem(pDC, nItem);
}

void VACompletionBox::DrawItem(CDC* pDC, int nItem, CRect* updated_region)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	try // Added to catch bogus m_ed issue with Resharper: case=25981
	{
		CImageList* pImageList;

		// Save dc state
		int nSavedDC = pDC->SaveDC();
		auto oldFont = pDC->SelectObject(m_font);

		// Get item state info
		LV_ITEM lvi;
		lvi.mask = LVIF_STATE;
		lvi.iItem = nItem;
		lvi.iSubItem = 0;
		lvi.stateMask = 0xffff; // get all state flags
		GetItem(&lvi);

		bool last_item = false;
		{
			int nextItem = GetNextItem(nItem, LVNI_BELOW);
			if (-1 == nextItem || nItem == nextItem)
				last_item = true;
		}

		WTString str;
		long icon;
		SymbolInfoPtr ptr = m_compSet->GetDisplayText(nItem, str, &icon); //  g_ExpData->FindItemByIdx(nItem);
		UINT scopeHash = ptr ? ptr->m_scopeHash : 0;
		UINT type = ptr ? ptr->m_type : icon;
		const UINT attrs = ptr ? ptr->mAttrs : 0;

		// Should the item be highlighted
		const bool bFocused = !!(lvi.state & LVIS_FOCUSED);
		const bool bSelected = bFocused && lvi.state & LVIS_SELECTED; // Prevent Selection w/o Focus

		// Get rectangles for drawing
		CRect client_rect;
		GetClientRect(client_rect);
		CRect rcHighlight, rcLabel, rcIcon, rcFull;
		GetItemRect(nItem, rcFull, LVIR_BOUNDS);
		rcHighlight = rcFull;
		GetItemRect(nItem, rcLabel, LVIR_LABEL);
		GetItemRect(nItem, rcIcon, LVIR_ICON);
		rcIcon.top += (rcIcon.Height() - ICON_WIDTH) / 2;
		if (gShellAttr->IsDevenv10OrHigher())
		{
			rcHighlight.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
			rcLabel.right = min(rcLabel.right, rcHighlight.right);
		}
		CRect rcCol(rcHighlight);

		// highlight full width of text column
		if (ICON_WIDTH)
		{
			rcLabel.left += ICON_SPACING; // add space between label and icon
			rcLabel.right += ICON_SPACING;
		}

		rcHighlight.left = rcLabel.left;

		// Draw the background color

		if (!g_baseExpScope || !Psettings->m_bBoldNonInheritedMembers || g_baseExpScope != scopeHash)
			pDC->SelectObject(m_font);
		else
			pDC->SelectObject(m_boldFont);

		CRect vsThemeRect = rcFull;
		if (UseVsTheme() && last_item)
		{
			// I think there's a bug in CListCtrl2008::GetSizingRect for which this
			// block is compensating.  Initial listbox height in C# has a fractional
			// last item with no text - so we treat the actual last item as taller
			// than normal.
			// Test by typing 's' in a c# method.
			// Repeat after: Tools | Options | esc (causes rebuild of box)
			vsThemeRect.bottom += vsThemeRect.Height(); // remainder space below last item
			vsThemeRect.bottom =
			    max(vsThemeRect.bottom,
			        client_rect.bottom); // just in case, but we won't use this always in the case DC has offset
			if (updated_region)
				updated_region->bottom = vsThemeRect.bottom;

			if (bSelected)
				rcHighlight.bottom = vsThemeRect.bottom;
		}

		bool overridePaintType = false;
		bool listboxColoringEnabled = Psettings->m_ActiveSyntaxColoring && Psettings->m_bEnhColorListboxes;
		if (listboxColoringEnabled && bSelected && gShellAttr->IsDevenv11OrHigher())
		{
			// [case: 65047] don't color selection
			listboxColoringEnabled = false;
			overridePaintType = true;
		}
		bool themed_draw = false;
		bool real_theme =
		    vs2010_active && AreThemesAvailable() && (Psettings->m_alwaysUseThemedCompletionBoxSelection >= 0);
	retry_theme:
		int emulated_theme = real_theme ? 0 : Psettings->m_alwaysUseThemedCompletionBoxSelection;
		if (bSelected)
		{
			COLORREF bgcolor = ::GetSysColor(COLOR_HIGHLIGHT);
			if (!m_compSet->IsFileCompletion() && listboxColoringEnabled)
			{
#define DELTA 25
				bgcolor = ::GetSysColor(COLOR_WINDOW);
				bgcolor = RGB((GetRValue(bgcolor) < 125) ? GetRValue(bgcolor) + DELTA * 2 : GetRValue(bgcolor) - DELTA,
				              (GetGValue(bgcolor) < 125) ? GetGValue(bgcolor) + DELTA * 2 : GetGValue(bgcolor) - DELTA,
				              (GetBValue(bgcolor) < 125) ? GetBValue(bgcolor) + DELTA * 2 : GetBValue(bgcolor) - DELTA);
				pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
			}
			else
			{
				if (gShellAttr->IsDevenv11OrHigher())
					pDC->SetTextColor(CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHTTEXT));
				else
					pDC->SetTextColor(::GetSysColor(COLOR_HIGHLIGHTTEXT));
			}

			if (real_theme || emulated_theme)
			{
				HTHEME theme = NULL;
				if (real_theme)
				{
					theme = OpenThemeData(m_hWnd, L"LISTVIEW");
					if (!theme)
					{
						real_theme = false;
						goto retry_theme;
					}
				}
				if (theme || emulated_theme)
				{
					if (real_theme)
					{
						// gmit: WARNING: drawing transparent background breaks Vista!!
						// 					if(__IsThemeBackgroundPartiallyTransparent(theme, TVP_TREEITEM,
						// 3/*LISS_SELECTED*/))
						// 						__DrawThemeParentBackground(m_hWnd, pDC->m_hDC, rcHighlight);
						DrawThemeBackground(theme, pDC->m_hDC, LVP_LISTITEM, 3 /*LISS_SELECTED*/, rcHighlight, NULL);
						CloseThemeData(theme);
					}
					else
					{
						const std::pair<double, COLORREF>* gradients;
						int gradients_count;
						COLORREF bordercolor;

						switch (emulated_theme)
						{
						case 1:
						case -1:
						default: {
							static const std::pair<double, COLORREF> gradients_1[] = {
							    std::make_pair(0, RGB(241, 247, 254)), std::make_pair(1, RGB(207, 228, 254))};
							static const COLORREF bordercolor_1 = RGB(132, 172, 221);
							gradients = gradients_1;
							gradients_count = countof(gradients_1);
							bordercolor = bordercolor_1;
						}
						break;
						case 2:
						case -2:
							if (gShellAttr->IsDevenv11OrHigher())
							{
								if (UseVsTheme())
								{
									bgcolor = CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHT);
									// [case: 66900] fix for occasional artifacts behind icons when item selected
									CRect rect = vsThemeRect;
									rect.right = rcHighlight.left;
									pDC->FillSolidRect(rect, g_IdeSettings->GetEnvironmentColor(L"Window", false));
									if(gShellAttr->IsDevenv17OrHigher())
										bgcolor = CVS2010Colours::GetThemeTreeColour(L"SelectedItemActive", FALSE);
									goto no_themes;
								}
								else
								{
									static const std::pair<double, COLORREF> gradients_1[] = {
									    std::make_pair(0.0, 0x00cdf3ffu), std::make_pair(1.0, 0x00cdf3ffu)};
									static const COLORREF bordercolor_1 = 0x0065c3e5;
									gradients = gradients_1;
									gradients_count = countof(gradients_1);
									bordercolor = bordercolor_1;
								}
							}
							else
								GetVS2010SelectionColours(FALSE, gradients, gradients_count, bordercolor);
							break;
						case 3:
						case -3: {
							static const std::pair<double, COLORREF> gradients_3[] = {
							    std::make_pair(0, RGB(247, 247, 247)), std::make_pair(1, RGB(217, 217, 217))};
							static const COLORREF bordercolor_3 = RGB(142, 142, 142);
							gradients = gradients_3;
							gradients_count = countof(gradients_3);
							bordercolor = bordercolor_3;
						}
						break;
						}

						if (UseVsTheme())
						{
							DrawVS2010Selection(*pDC, vsThemeRect, gradients, gradients_count, bordercolor,
							                    std::bind(&DrawVS2010MenuItemBackground, _1, client_rect, false,
							                              (const CRect*)NULL, 0, false));
						}
						else
						{
							CBrush brush;
							brush.CreateSolidBrush(::GetSysColor(COLOR_WINDOW));
							DrawVS2010Selection(*pDC, rcHighlight, gradients, gradients_count, bordercolor,
							                    std::bind(&CDC::FillRect, _1, _2, &brush));
						}
					}

					themed_draw = true;
				}
				else
					goto no_themes;
			}
			else
			{
			no_themes:
				pDC->FillSolidRect(rcHighlight, bgcolor);
			}

			if (!themed_draw)
				pDC->SetBkColor(bgcolor);
		}
		else
		{
			// item not selected
			if (UseVsTheme())
			{
				if (gShellAttr->IsDevenv10())
				{
					pDC->SetTextColor(CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_TEXT_ACTIVE));
					_ASSERTE(CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_BEGIN) ==
					         CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_END));
					pDC->FillSolidRect(vsThemeRect,
					                   CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_BEGIN));
				}
				else
				{
					pDC->SetTextColor(g_IdeSettings->GetEnvironmentColor(L"WindowText", false));
					pDC->FillSolidRect(vsThemeRect, g_IdeSettings->GetEnvironmentColor(L"Window", false));
				}
			}
			else if (gShellAttr->IsDevenv10() && abs(emulated_theme) == 2 && UseVsTheme())
				DrawVS2010MenuItemBackground(*pDC, rcFull, true, &client_rect, 0, false);
			else if (gShellAttr->IsDevenv11OrHigher())
			{
				pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
				pDC->FillSolidRect(rcFull, ::GetSysColor(COLOR_WINDOW));
			}
			else
			{
				pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
				pDC->FillSolidRect(rcHighlight, ::GetSysColor(COLOR_WINDOW));
			}
		}

		bool set_text_colour = false;
		if (!m_compSet->IsFileCompletion())
		{
			COLORREF clr = WTColorFromType(ptr ? (int)ptr->m_type : icon);

			if (clr == 0xdead && listboxColoringEnabled && ptr && ptr->m_type == ET_SCOPE_SUGGESTION &&
			    ptr->mSymStr[0] == '#' && ::IsValidHashtag(ptr->mSymStr))
			{
				// [case: 86085] coloring of hashtag scope suggestions
				clr = g_IdeSettings->GetDteEditorColor(L"VA Hashtag", TRUE);
				if (UINT_MAX == clr)
					clr = 0xdead;
				else
					overridePaintType = true;
			}

			if (clr != 0xdead && listboxColoringEnabled && !(g_currentEdCnt && g_currentEdCnt->m_txtFile) &&
			    type != ET_EXPAND_TEXT)
			{
				if (!themed_draw)
				{
					// make sure color is visible against selected bg color
					COLORREF bgclr = pDC->GetBkColor();
					int cdiff = abs(GetRValue(clr) - GetRValue(bgclr)) + abs(GetGValue(clr) - GetGValue(bgclr)) +
					            abs(GetBValue(clr) - GetBValue(bgclr));
					if (cdiff >= 100)
						pDC->SetTextColor(clr);
				}
				else
					pDC->SetTextColor(clr);
			}
			else
				set_text_colour = true;
		}
		else
			set_text_colour = true;
		if (set_text_colour)
		{
			if (themed_draw)
			{
				COLORREF clr = RGB(0, 0, 0);
				if (real_theme)
				{
					HTHEME theme = ::OpenThemeData(m_hWnd, L"LISTVIEW");
					if (theme)
					{
						::GetThemeColor(theme, LVP_LISTITEM, 3 /*LISS_SELECTED*/, TMT_TEXTCOLOR, &clr);
						::CloseThemeData(theme);
					}
					else if (Psettings->m_alwaysUseThemedCompletionBoxSelection)
						clr = RGB(0, 0, 0);
				}
				else
					clr = RGB(0, 0, 0); // gmit: colour used if text colouring is disabled!
				pDC->SetTextColor(clr);
			}
			else if(bSelected && gShellAttr->IsDevenv17OrHigher())
			{
				pDC->SetTextColor(CVS2010Colours::GetVS2010Colour(
					(g_IdeSettings && g_IdeSettings->IsBlueVSColorTheme15()) ? 
					VSCOLOR_COMMANDBAR_TEXT_ACTIVE : VSCOLOR_HIGHLIGHTTEXT));
			}
		}

		// Set clip region
		rcCol.right = rcCol.left + GetColumnWidth(0);
		CRgn rgn;
		rgn.CreateRectRgnIndirect(&rcCol);
		pDC->SelectClipRgn(&rgn);
		rgn.DeleteObject();

		// Draw normal and overlay icon
		if (ICON_WIDTH)
		{
#ifdef _WIN64
			auto drawBgIconInCorner = [&]() {
				auto mon = gImgListMgr->GetMoniker(ICONIDX_TOMATO, false, true);
				if (mon)
				{
					auto dpiOver2 = (UINT)VsUI::DpiHelper::GetDeviceDpiY() / 2;
					COLORREF bgColor = gImgListMgr->GetBackgroundColor(ImageListManager::bgList);

					if (m_tomato.m_hObject && m_tomatoDpi != dpiOver2)
					{
						m_tomato.DeleteObject();
					}

					if (m_tomato.m_hObject || SUCCEEDED(gImgListMgr->GetMonikerImage(m_tomato, *mon, bgColor, dpiOver2, false, false, false)))
					{
						m_tomatoDpi = dpiOver2;
						BITMAP bmpInfo;
						if (m_tomato.GetBitmap(&bmpInfo))
						{
							CSaveDC saveDc(*pDC);
							CBrush bgBrush;
							bgBrush.CreateSolidBrush(bgColor);
							CPen bgPen;
							bgPen.CreatePen(PS_SOLID, 1, bgColor);

							pDC->SelectObject(bgBrush);
							pDC->SelectObject(bgPen);

							CRect rcTomato = rcIcon;
							rcTomato.left += ICON_WIDTH - bmpInfo.bmWidth;
							rcTomato.bottom = rcTomato.top + bmpInfo.bmHeight;

							CRect rcTomatoBG = rcTomato;
							rcTomatoBG.left -= VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
							rcTomatoBG.bottom += VsUI::DpiHelper::LogicalToDeviceUnitsY(1);
							pDC->Ellipse(&rcTomatoBG);

							ThemeUtils::DrawImage(pDC->m_hDC, m_tomato, rcTomato.TopLeft());
						}
					}
				}
			};
#endif

			pImageList = GetImageList(LVSIL_SMALL);
			if (pImageList)
			{
				int img = GetTypeImgIdx(
				    type, attrs,
				    true); // (s_popType==ET_EXPAND_VSNET || s_popType == ET_EXPAND_TEXT)?type:(s_popType==ET_SUGGEST ||
				           // s_popType == ET_AUTOTEXT || s_popType == ET_SUGGEST_AUTOTEXT || s_popType ==
				           // ET_SUGGEST_BITS)?ICONIDX_SUGGESTION:GetTypeImgIdx(type);
				if (!IS_VSNET_IMG_TYPE(type) && g_currentEdCnt)
				{
#ifndef _WIN64
					if (Psettings->mUseTomatoBackground && ET_VS_SNIPPET != type)
					{
						// Draw a pale tomato back splash to distinguish between theirs and our suggestions.
						CImageList* pIl = gImgListMgr->GetImgList(ImageListManager::bgList);
						if (pIl)
							pIl->Draw(pDC, ICONIDX_TOMATO_BACKGROUND,
							          CPoint(rcIcon.left, rcIcon.top + VsUI::DpiHelper::LogicalToDeviceUnitsY(1)),
							          ILD_TRANSPARENT); // do not blend icons, makes VS.NET2005 icons look bad
					}
#endif
				}
				else
					rcIcon.left += VsUI::DpiHelper::LogicalToDeviceUnitsX(
					    1); // Offset their images to the right to align with ours.  (ours are to the right one pixel
					        // for the for visibility with the tomato background)

				pImageList->Draw(pDC, img, CPoint(rcIcon.left, rcIcon.top + VsUI::DpiHelper::LogicalToDeviceUnitsY(1)),
				                 ILD_TRANSPARENT); // do not blend icons, makes VS.NET2005 icons look bad
#ifdef _WIN64
				if (Psettings->mUseTomatoBackground && ET_VS_SNIPPET != type)
				{
					drawBgIconInCorner();
				}
#endif 
			}
		}

		// Draw item label - Column 0
		str.ReplaceAll("\n", "\\n");
		int autoTextShortcut = str.Find(AUTOTEXT_SHORTCUT_SEPARATOR);
		if (autoTextShortcut != -1)
			str = str.Mid(0, autoTextShortcut);
		// this loop is duplicated in FastComboBoxListCtrl.cpp
		for (int i = 0; i < str.length(); i++)
		{
			if ((str[i] & 0x80) || !isprint(str[i]))
			{
				if (str[i] == '\n')
					str.SetAt(i, 0x1f);
				else if (str[i] & 0x80)
					str.SetAt(i, str[i]); // allow accent characters and copyright symbol
				else
					str.SetAt(i, ' ');
			}
		}
		//	COLORREF clr = Psettings->m_colors[C_Var].c_fg;
		//	switch(type&TYPEMASK){
		//	case UNDEF:
		//		clr = 0xdead;
		//		break;
		//
		//	case DEFINE:
		//		clr = Psettings->m_colors[C_Macro].c_fg;
		//		break;
		//	case VAR:
		//		clr = Psettings->m_colors[C_Var].c_fg;
		//		break;
		//	case FUNC:
		//		clr = Psettings->m_colors[C_Function].c_fg;
		//		break;
		////	case TYPE: case CLASS: case RESWORD:
		//	default:
		//		clr = Psettings->m_colors[C_Type].c_fg;
		//	}
		////	if(s_popType == 2)
		////		clr = Psettings->m_colors[C_Text].c_fg;
		//
		//	if(clr != 0xdead && !(g_currentEdCnt && g_currentEdCnt->m_txtFile) && listboxColoringEnabled && s_popType !=
		// ET_EXPAND_TEXT)
		//	{
		//		// make sure color is visible against selected bg color
		//		COLORREF bgclr = pDC->GetBkColor();
		//		int cdiff = abs(GetRValue(clr) - GetRValue(bgclr)) +
		//			abs(GetGValue(clr) - GetGValue(bgclr)) +
		//			abs(GetBValue(clr) - GetBValue(bgclr));
		//		if(cdiff >= 100)
		//			pDC->SetTextColor(clr);
		//	}
		//	pDC->TextOut(rcLabel.left, rcLabel.top, ptr->m_data);
		// testing mbcs compatibility, still has problems. work in progress...
		str = ::TruncateString(str);
		const CStringW wStr(str.Wide());
		rcLabel.left += VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
		{
			if (!overridePaintType)
			{
				// [case: 95401]
				if (ET_VS_SNIPPET == type || ET_AUTOTEXT == type)
					overridePaintType = true;
				else if (IMG_IDX_TO_TYPE(ICONIDX_REFACTOR_RENAME) == type)
					overridePaintType = true;
			}

			TempPaintOverride t(overridePaintType);
			::VaDrawTextW(pDC->GetSafeHdc(), wStr, rcLabel,
			              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP | DT_VCENTER);
		}

		// Restore dc
		pDC->SelectObject(oldFont);
		pDC->RestoreDC(nSavedDC);
		if (bFocused && !bSelected) // Focus rect not needed if selected. Per current implementation.
		{
			if (UseVsTheme())
			{
				CPenDC pen(*pDC, CVS2010Colours::GetVS2010Colour(VSCOLOR_HIGHLIGHT));
				CBrushDC br(*pDC);
				pDC->Rectangle(&rcHighlight);
			}
			else if (abs(emulated_theme) == 2)
			{
				if (GetStyle() & WS_VSCROLL)
					rcHighlight.right += VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
				else
					rcHighlight.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(1);
				CBrush focusBrush(gShellAttr->IsDevenv11OrHigher()
				                      ? 0x0065c3e5
				                      : CVS2010Colours::GetVS2010Colour(VSCOLOR_COMMANDBAR_SELECTED_BORDER));
				pDC->FrameRect(&rcHighlight, &focusBrush);
			}
			else
			{
				rcHighlight.right -= VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
				pDC->DrawFocusRect(rcHighlight);
			}
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("SACS:");
	}
}

void VACompletionBox::OnTimer(UINT_PTR nIDEvent)
{
	if (!g_FontSettings)
	{
		// [case: 138039]
		return;
	}

	try // Added to catch bogus m_ed issue with Resharper: case=25981
	{
		switch (nIDEvent)
		{
		case Timer_DismissIfCaretMoved: {
			KillTimer(nIDEvent);
			if (!IsWindowVisible())
				return;
			if (gShellAttr->IsDevenv10OrHigher())
				return; // Cleared on VAM_OnChangeScrollInfo
			HWND hFoc = ::VAGetFocus();
			if (hFoc != GetSafeHwnd())
			{
				if (hFoc != m_compSet->m_ed->GetSafeHwnd() && hFoc != m_compSet->m_tBar->GetSafeHwnd() &&
				    hFoc != m_compSet->m_tBarContainer->GetSafeHwnd() &&
				    hFoc != m_compSet->m_expBoxContainer->GetSafeHwnd())
					m_compSet->Dismiss();

				CPoint pt;
				::GetCaretPos(&pt);
				m_compSet->m_ed->vClientToScreen(&pt);

				if (pt != m_compSet->m_caretPos && !(GetKeyState(VK_BACK) & 0x1000))
				{
					if (!g_CompletionSet->GetExpContainsFlags(
					        VSNET_TYPE_BIT_FLAG)) // Fixes <tag prop=" , they leave the list up after typing "
						m_compSet->Dismiss();
				}
			}
			if (IsWindowVisible())
				SetTimer(nIDEvent, 100, NULL);
			return;
		}
		case Timer_UpdateTooltip:
		case Timer_RepositionToolTip: // this one will only reposition tooltip; won't hide/show it
		{
			const bool only_reposition = (nIDEvent == Timer_RepositionToolTip) && m_tooltip;
			KillTimer(nIDEvent);
			if (!IsWindowVisible())
				return;

			int item = (int)(uintptr_t)GetFocusedItem() - 1;
			if (item == -1)
				item = 0; // Display tip for first item if nothing is selected/focused ie:m_isDef

			bool shouldColorTooltip = false;
			WTString tipText;
			if (!only_reposition)
			{
				tipText = m_compSet->GetDescriptionText(item, shouldColorTooltip);
				if (gShellAttr->IsDevenv14OrHigher() && (gTypingDevLang == CS || gTypingDevLang == VB) &&
				    StartsWith(tipText, "...", FALSE))
				{
					// [case: 94723] [case: 96591] give async tip some time to load
					SetTimer(nIDEvent, 100, NULL);
					return;
				}

				WTString wtxt = tipText;
				wtxt.ReplaceAll("\r", " ");
				tipText = wtxt;
			}

			if (only_reposition || tipText.GetLength())
			{
				CRect ritem, rw;
				GetWindowRect(&rw);
				GetItemRect(item, ritem, LVIR_SELECTBOUNDS);
				if (!LC2008_DisableAll() && LC2008_DoMoveTooltipOnVScroll())
				{
					CRect firstvisibleitem(0, -100000, 0, -100000);
					CRect lastvisibleitem(0, 100000, 0, 100000);
					GetItemRect(GetTopIndex(), firstvisibleitem, LVIR_SELECTBOUNDS);
					GetItemRect(GetTopIndex() + GetCountPerPage() - 1, lastvisibleitem, LVIR_SELECTBOUNDS);
					if (ritem.top < firstvisibleitem.top)
						ritem = firstvisibleitem;
					if (ritem.top > lastvisibleitem.top)
						ritem = lastvisibleitem;
				}

				CPoint pt(rw.right, rw.top + ritem.top);
				BOOL color = !(g_currentEdCnt && g_currentEdCnt->m_txtFile);
				if (m_tooltip->GetSafeHwnd() && UseVsTheme() && !only_reposition && !m_tooltip->IsWindowVisible())
				{
					// [case: 65178]
					m_tooltip->MoveWindow(pt.x, pt.y, 1, 1, 0);
				}

				if (!m_tooltip)
				{
					m_tooltip = new ArgToolTip(GetSafeHwnd());
					m_tooltip->dont_close_on_ctrl_key = !LC2008_DisableAll() && LC2008_HasCtrlFadeEffect();
					m_tooltip->avoid_hwnd = m_hWnd;
					if (!LC2008_DisableAll())
					{
						if (LC2008_DoSetSaveBits())
						{
							::SetClassLong(m_tooltip->m_hWnd, GCL_STYLE,
							               int(::GetClassLong(m_tooltip->m_hWnd, GCL_STYLE) | CS_SAVEBITS));
							// WinXP WS_EX_LAYERED fix
						}
					}

					if (gShellAttr->IsDevenv10OrHigher())
					{
						// [case: 62688]
						if (gShellAttr->IsDevenv11OrHigher() && g_IdeSettings)
						{
							// [case: 65870]
							m_tooltip->UseVsEditorTheme();
						}
						else if (!Psettings->m_ActiveSyntaxColoring || !Psettings->m_bEnhColorTooltips)
						{
							// in vs2010, only use their color if syntax coloring is not enabled on tooltips
							// (just to avoid radical change in behavior compared to the last 2 years)
							m_tooltip->OverrideColor(true, CVS2010Colours::GetVS2010Colour(VSCOLOR_MENUTEXT),
							                         CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_BACKGROUND),
							                         CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_BORDER));
						}
					}
				}
				BOOL ourTip = FALSE;
				{
					WTString dispTxt;
					long glyf;
					SymbolInfoPtr inf = m_compSet->GetDisplayText(item, dispTxt, &glyf);
					if (inf && !IS_VSNET_IMG_TYPE(inf->m_type) && ET_VS_SNIPPET != inf->m_type)
						ourTip = TRUE;
				}
				TempSettingOverride<bool> ov(&Psettings->m_bEnhColorTooltips, false, !shouldColorTooltip);
				if (!only_reposition)
					m_tooltip->Display(&pt, tipText.c_str(), NULL, NULL, 1, 1, false, false, color, ourTip);

				CRect tooltiprect;
				m_tooltip->GetWindowRect(tooltiprect);
				tooltiprect.MoveToXY(pt);
				bool position_flipped = false;
				CRect desktoprect = g_FontSettings->GetDesktopEdges(
				    (m_compSet && m_compSet->m_ed.get()) ? m_compSet->m_ed.get() : NULL);
				if ((tooltiprect & desktoprect) != tooltiprect)
				{ // tooltip is out of the screen on the right
					tooltiprect.MoveToX(rw.left - tooltiprect.Width());
					if ((tooltiprect & desktoprect) == tooltiprect)
					{ // prefer showing tooltip on the right if it goes out of the screen on the left
						pt = tooltiprect.TopLeft();
						position_flipped = true;
					}
				}

				if (position_flipped || only_reposition)
					m_tooltip->SetWindowPos(NULL, pt.x, pt.y, 0, 0, SWP_NOSIZE | SWP_NOREPOSITION | SWP_NOACTIVATE);

				if (!LC2008_DisableAll() &&
				    (LC2008_HasCtrlFadeEffect() || LC2008_HasHorizontalResize() || LC2008_HasVerticalResize()))
				{
					AddCompanion(m_tooltip->m_hWnd, position_flipped ? cdxCDynamicWnd::mdNone : cdxCDynamicWnd::mdRepos,
					             cdxCDynamicWnd::mdNone, false);
				}
			}
			else if (m_tooltip && m_tooltip->GetSafeHwnd())
				m_tooltip->ShowWindow(SW_HIDE);
			return;
		}
		case Timer_ReDisplay:
			KillTimer(nIDEvent);
			try
			{
				// Typing event caused Display to return early, Redisplay to filter and display.
				if (g_currentEdCnt)
					g_currentEdCnt->GetBuf(TRUE); // fixes FixUpFnCall in c#
				m_compSet->DisplayList(TRUE);
			}
			catch (...)
			{
				m_compSet->Dismiss();
			}
			break;
		default:
			KillTimer(nIDEvent);
			break;
		}

		CListCtrl2008::OnTimer(nIDEvent);
	}
	catch (...)
	{
		VALOGEXCEPTION("SACS:");
	}
}

void VACompletionBox::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CListCtrl2008::OnVScroll(nSBCode, nPos, pScrollBar);

	if (!LC2008_DisableAll() && LC2008_DoMoveTooltipOnVScroll())
	{
		OnTimer(Timer_RepositionToolTip);
	}
}

void VACompletionBox::OnSizing(UINT fwSide, LPRECT pRect)
{
	CListCtrl2008::OnSizing(fwSide, pRect);

	//	if(LC2008_EnsureVisibleOnResize())
	//		OnTimer(Timer_RepositionToolTip);
}

void VACompletionBox::OnSize(UINT nType, int cx, int cy)
{
	CListCtrl2008::OnSize(nType, cx, cy);

	if (LC2008_EnsureVisibleOnResize())
		OnTimer(Timer_RepositionToolTip);

	// 	if(gShellAttr->IsDevenv11OrHigher() && UseVsTheme())
	// 	{
	// 		// cut border for VS11; its native completion box doesn't have one
	// 		CRect wrect;
	// 		GetWindowRect(&wrect);
	// 		wrect.MoveToXY(0, 0);
	// 		wrect.DeflateRect(1, 1, 1, 1);
	// 		SetWindowRgn(::CreateRectRgnIndirect(&wrect), false);
	// 	}
}

LRESULT VACompletionBox::OnReturnFocusToEditor(WPARAM wparam, LPARAM lparam)
{
	MSG msg;
	if (::PeekMessage(&msg, m_hWnd, WM_RETURN_FOCUS_TO_EDITOR, WM_RETURN_FOCUS_TO_EDITOR, PM_NOREMOVE))
	{
		return 0; // don't do anything until only one WM_RETURN_FOCUS_TO_EDITOR is waiting
	}

	SetTimer(Timer_RepositionToolTip, 50, NULL); // update tooltip

	if (::VAGetFocus() != m_compSet->m_ed->m_hWnd)
	{
		if (m_tooltip && m_tooltip->m_hWnd)
			mySetProp(m_tooltip->m_hWnd, "__VA_dont_close_on_kill_focus", (HANDLE)1);
		m_compSet->m_ed->vSetFocus();
		if (m_tooltip && m_tooltip->m_hWnd)
			mySetProp(m_tooltip->m_hWnd, "__VA_dont_close_on_kill_focus", 0);
	}

	return 0;
}

int VACompletionBox::GetFrameWidth()
{
	// match frame style used in VACompletionBox::Create
	if (GetStyle() & WS_THICKFRAME)
		return VsUI::DpiHelper::GetSystemMetrics(SM_CXFRAME);
	return VsUI::DpiHelper::GetSystemMetrics(SM_CXBORDER);
}

bool VACompletionBox::UseVsTheme() const
{
	return CVS2010Colours::IsVS2010AutoCompleteActive();
}

void VACompletionBox::HideTooltip()
{
	if (m_tooltip && m_tooltip->GetSafeHwnd() && m_tooltip->IsWindowVisible())
		m_tooltip->ShowWindow(SW_HIDE);
}

int VACompletionBoxOwner::GetItemHeight(int code /*= LVIR_BOUNDS*/)
{
	UpdateMetrics();

	switch (code)
	{
	case LVIR_LABEL:
		return label_height;
	case LVIR_ICON:
		return icon_height;
	default:
		return bounds_height;
	}
}

void VACompletionBoxOwner::UpdateMetrics()
{
	auto scope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);
	int currDpi = VsUI::DpiHelper::GetDeviceDpiX();

	icon_height = VsUI::DpiHelper::GetSystemMetrics(SM_CYSMICON);

	int px2 = VsUI::DpiHelper::LogicalToDeviceUnitsX(2);
	int px4 = VsUI::DpiHelper::LogicalToDeviceUnitsX(4);

	if (g_FontSettings)
	{
		if (font_settings_generation != g_FontSettings->GetSettingsGeneration() || dpi != currDpi)
		{
			CClientDC cDC(this);
			font_obj.Update((uint)currDpi, VaFontType::ExpansionFont);
			CFontDC font(cDC, font_obj);
			TEXTMETRIC tm;
			memset(&tm, 0, sizeof(tm));
			cDC.GetTextMetrics(&tm);

			label_height = tm.tmHeight + px2;
			bounds_height = max<LONG>(tm.tmHeight + px2, icon_height) + px4;
			font_settings_generation = g_FontSettings->GetSettingsGeneration();
			dpi = currDpi;
		}
	}
	else
	{
		label_height = icon_height;
		bounds_height = icon_height + px4;
	}
}

LRESULT VACompletionBoxOwner::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT rslt = __super::WindowProc(message, wParam, lParam);
	if (message == WM_MEASUREITEM)
	{
		((LPMEASUREITEMSTRUCT)lParam)->itemHeight = (uint)GetItemHeight();
	}

	return rslt;
}
