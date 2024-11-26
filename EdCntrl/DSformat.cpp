#include "stdafxed.h"
#include "resource.h"
#include "settings.h"
#include "RegKeys.h"
#include "edcnt.h"
#include "fontsettings.h"
#include "VACompletionSet.h"
#include "VACompletionBox.h"
#include "VAParse.h"
#include "ScreenAttributes.h"
#include "DevShellAttributes.h"
#include "Registry.h"
#include "WindowUtils.h"
#include "FileTypes.h"
#include "DBLock.h"
#include "FindReferences.h"
#include "FreezeDisplay.h"
#include "fdictionary.h"
#include "myspell\WTHashList.h"
#include "mainThread.h"
#include "SyntaxColoring.h"
#include "project.h"
#include "WPF_ViewManager.h"
#include "VaService.h"
#include "LiveOutlineFrame.h"
#include "ColorSyncManager.h"
#include "Colourizer.h"
#include "EdDll.h"
#include "HookCode.h"

extern BOOL g_inPaint;
extern EdCnt* g_paintingEdCtrl;
EdCnt* g_printEdCnt = NULL;
int g_vaCharWidth[256];
int g_screenXOffset = 0;
int g_screenMargin = 0;


// MakePtr is a macro that allows you to easily add to values (including
// pointers) together without dealing with C's pointer arithmetic.  It
// essentially treats the last two parameters as DWORDs.  The first
// parameter is used to typecast the result to the appropriate pointer type.
#define MakePtr(cast, ptr, addValue) (cast)((DWORD)(ptr) + (DWORD)(addValue))
HFONT g_TxtFontObj = NULL;
void GetFontInfo(HDC dc)
{
	try
	{
		HFONT cFont = (HFONT)::GetCurrentObject(dc, OBJ_FONT);
		if (dc && cFont && g_TxtFontObj != cFont && cFont != g_FontSettings->m_TxtFont.m_hObject)
		{
			if (cFont == g_FontSettings->m_TxtFontItalic)
				return;
			if (cFont == g_FontSettings->m_TxtFontBOLD)
				return;

			if (g_TxtFontObj && gShellAttr && gShellAttr->IsDevenv() &&
			    GetTextColor(dc) != Psettings->m_colors[C_Text].c_fg)
			{
				// Bail if not identifier and not first time. case=31119
				// but don't do this in VC6. case=31192
				return;
			}

			g_TxtFontObj = cFont;
			LOGFONTW lf;
			GetObjectW(cFont, sizeof(LOGFONTW), &lf);
			GetCharWidth32W(dc, 0, 255, g_vaCharWidth);

			g_FontSettings->UpdateTextMetrics(dc);

			if (g_FontSettings->m_TxtFont.m_hObject)
				g_FontSettings->m_TxtFont.DeleteObject();
			g_FontSettings->m_TxtFont.CreateFontIndirect(&lf);
			if (Psettings)
				wcscpy(Psettings->m_srcFontName, lf.lfFaceName);

			SelectObject(dc, cFont);

			lf.lfWeight = FW_BOLD;
			if (g_FontSettings->m_TxtFontBOLD.m_hObject)
				g_FontSettings->m_TxtFontBOLD.DeleteObject();
			g_FontSettings->m_TxtFontBOLD.CreateFontIndirect(&lf);
			SelectObject(dc, g_FontSettings->m_TxtFontBOLD.m_hObject);

			lf.lfItalic = TRUE;
			lf.lfWeight = FW_NORMAL;
			if (g_FontSettings->m_TxtFontItalic.m_hObject)
				g_FontSettings->m_TxtFontItalic.DeleteObject();
			g_FontSettings->m_TxtFontItalic.CreateFontIndirect(&lf);
			SelectObject(dc, g_FontSettings->m_TxtFontItalic.m_hObject);

			if (g_loggingEnabled)
			{
				MyLog("FontInfo: %s, %d, %d", (LPCTSTR)CString(lf.lfFaceName), lf.lfHeight, g_vaCharWidth[' ']);
				MyLog("CharWidth/Height: %d, %d", g_FontSettings->GetCharWidth(), g_FontSettings->GetCharHeight());
			}

			SelectObject(dc, cFont);
		}
	}
	catch (...)
	{
	}
}

CPoint g_CursorPos;
WTString g_StrFromCursorPos;
BOOL g_StrFromCursorPosUnderlined = FALSE;

void UnderLine(HDC dc, int x, int y, int x2, COLORREF clr, BOOL isComment = TRUE)
{
	extern CPoint g_caretPoint;
	if (y == (g_caretPoint.y + g_FontSettings->GetCharHeight() - 2) && x < g_caretPoint.x && x2 > g_caretPoint.x)
		return;
	if (isComment && g_CursorPos.y < y && g_CursorPos.y >= (y - g_FontSettings->GetCharHeight()) && x < g_CursorPos.x &&
	    x2 > g_CursorPos.x)
		g_StrFromCursorPosUnderlined = TRUE; // only in comments
	if ((x2 - x) > 3000)                     // sanity check
	{
		_ASSERTE(!"assert in Underline");
		return;
	}
	x = max(x, g_screenMargin);
	x2 = max(x2, g_screenMargin);

	int i;

	if (gShellAttr->IsMsdev())
	{
		// Our underlining in VC6 is:
		//   **  **
		// **  **
		// a 2-pixel-high square wave, with a period of 4 pixels, with no
		// restrictions on phase.
		//

		// top of underline has no pixels between it and the baseline.
		y += 1;

		for (i = x + 2; i < x2; i += 4)
		{
			SetPixel(dc, i, y, clr);
			SetPixel(dc, i + 1, y, clr);
		}
		for (i = x; i < x2; i += 4)
		{
			SetPixel(dc, i, y + 1, clr);
			SetPixel(dc, i + 1, y + 1, clr);
		}
	}
	else
	{
		// Match VS underlining, which is:
		//  *   *
		// * * * * *
		//    *   *
		// a 3-pixel-high triangle wave, with a period of 4 pixels, such that the
		// first zero crossing occurs where x-pos is a multiple of 4.
		//
		// There is one pixel between the top of the wave and the bottom of text.
		// Letters with descenders (gyqpj) are partially over-drawn by the underline.
		//

		// top of underline has 1-pixel between it and the baseline.
		y += 2;

		i = x;
		switch (i % 4)
		{
		case 1:
			SetPixel(dc, i++, y, clr);
			// fall through
		case 2:
			SetPixel(dc, i++, y + 1, clr);
			// fall through
		case 3:
			SetPixel(dc, i++, y + 2, clr);
			// fall through
		case 0:
			break;
		}

		int loops = (x2 - i) / 4;
		while (loops--)
		{
			SetPixel(dc, i++, y + 1, clr);
			SetPixel(dc, i++, y, clr);
			SetPixel(dc, i++, y + 1, clr);
			SetPixel(dc, i++, y + 2, clr);
		}
		if (i < x2)
			SetPixel(dc, i++, y + 1, clr);
		if (i < x2)
			SetPixel(dc, i++, y, clr);
		if (i < x2)
			SetPixel(dc, i++, y + 1, clr);
		if (i < x2)
			SetPixel(dc, i++, y + 2, clr);
	}
}

#if !defined(RAD_STUDIO)
static bool PatchFile(HMODULE hMod, int filenumber, FARPROC origProc, FARPROC ourProc)
{
// #TODO3264 patching 64 bits
#ifndef _WIN64
	PIMAGE_NT_HEADERS pNTHeader;
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hMod;
	DWORD base = (DWORD)hMod;
	bool retval = false;
	Log("PatchFile");
	if (!(hMod && origProc && ourProc))
		return FALSE;

	if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
	{
		pNTHeader = MakePtr(PIMAGE_NT_HEADERS, dosHeader, dosHeader->e_lfanew);

		// First, verify that the e_lfanew field gave us a reasonable
		// pointer, then verify the PE signature.
		__try
		{
			if (pNTHeader->Signature != IMAGE_NT_SIGNATURE)
			{
				return false;
			}
		}
		__except (TRUE) // Should only get here if pNTHeader (above) is bogus
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	for (int nddir = 0; nddir <= IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR; nddir++)
	{
		// go straight to the IAT directory
		PIMAGE_THUNK_DATA pThunkIAT =
		    MakePtr(PIMAGE_THUNK_DATA, base, pNTHeader->OptionalHeader.DataDirectory[nddir].VirtualAddress);
		DWORD endOfIAT = (DWORD)pThunkIAT + pNTHeader->OptionalHeader.DataDirectory[nddir].Size;

		for (; (DWORD)pThunkIAT <= endOfIAT; pThunkIAT++)
		{
#if _MSC_VER > 1200
			if (pThunkIAT->u1.Function == (DWORD)origProc)
#else
			if (pThunkIAT->u1.Function == (PDWORD)origProc)
#endif
			{
				DWORD oldProtFlags;
				if (VirtualProtect(pThunkIAT, sizeof(PDWORD), PAGE_EXECUTE_READWRITE, &oldProtFlags))
				{
					//				if (filenumber != -1)
					//					g_thunkIAT[filenumber] = pThunkIAT;
#if _MSC_VER > 1200
					pThunkIAT->u1.Function = (DWORD)ourProc;
#else
					pThunkIAT->u1.Function = (PDWORD)ourProc;
#endif
					VirtualProtect(pThunkIAT, sizeof(PDWORD), oldProtFlags, &oldProtFlags);
					Log("Patched");
					return true;
				}
				else
				{
					return false;
				}
			}
			// problem debugging under Win95/98:
			// Win98 MDISysAccel: g_pfnTransAccel 0x82df2350 (BFF557CB) s/b 0x82de1a08 0x50002618 (brk when pThunkIAT is
			// this) set a brkpt for pThunkIAT == 50002618 and change the origproc and g_pfnTransAccel to
			// pThunkIAT->u1.Function
		}
	}

	return retval;
#else
	return true;
#endif
}
#endif

int ShouldColorHWnd(HWND h)
{
	if (myGetProp(h, "__VA_do_not_colour"))
		return FALSE;

	HINSTANCE inst = (HINSTANCE)GetWindowLongPtr(h, GWLP_HINSTANCE);
	const WTString cls = GetWindowClassString(h);
	if (cls == "VsTipWindow" || cls == "VcTipWindow")
		return PaintType::VS_ToolTip; // Catch tip drawing not caused by wm_paint,  case: 15124
	if (cls == "VsTextEditPane")
	{
		WTString caption = GetWindowTextString(h);
		WTString pcls = GetWindowClassString(::GetParent(h));
		if (pcls == "OutputWindowClass")
			return PaintType::None;
		else if (pcls == "GenericPane")
			return PaintType::FindInFiles; // Find-in-files
	}

	if (inst == gVaDllApp->GetVaAddress())
	{
		int id = ::GetDlgCtrlID(h);

		if (cls == "tooltips_class32")
		{
			if (gColorSyncMgr && gColorSyncMgr->GetActiveVsTheme() != ColorSyncManager::avtLight &&
			    gColorSyncMgr->GetActiveVsTheme() != ColorSyncManager::avtBlue)
			{
				// [case: 71740] don't color outline and vaview tooltips in vs2012 if light theme is not active
				return PaintType::None;
			}

			// Hack: To catch hovering outline tip
			return PaintType::ToolTip;
		}
		else if (cls == "SysTabControl32")
			return PaintType::None;
#ifdef _DEBUG
		if ((cls == "VsTipWindow" || cls == "VcTipWindow" || cls == "Auto-Suggest Dropdown" ||
		     cls == "VsCompletorPane"))
		{
			return PaintType::ToolTip;
		}
#endif // _DEBUG

		switch (id)
		{
		case IDC_TREE1:
		case IDC_LIST1:
			return PaintType::View;
		case 0x3e8: // dropped members ComboBox list
		case 0:     // dropped members ComboBox list
		{
			if (g_CompletionSet && g_CompletionSet->m_expBox && h == g_CompletionSet->m_expBox->GetSafeHwnd())
				return FALSE; // our expansion box does its own coloring
			if (id == 0)
				return FALSE;
			if (cls.GetLength() && cls[0] == '#')
				return FALSE; // context menu
			if (cls == "ComboLBox")
				return FALSE;
			if (cls == "LiteTreeView32")
			{
				return FALSE; // script
			}
		}
			if (g_currentEdCnt && Is_C_CS_VB_File(g_currentEdCnt->m_ftype))
				return PaintType::ListBox;
			return PaintType::None;
		case IDC_KEY: // Don't color the "Enter Key" dialog
			return FALSE;
		case IDC_FILTEREDIT:
		case IDC_GENERICCOMBO:
		case IDC_LIST2:
		case IDC_FILELIST:
			return FALSE;
		default:
			if (cls == "SysTreeView32")
				return PaintType::View;

			if (cls == "SysListView32")
			{
				// [case: 85578]
				const WTString caption(GetWindowTextString(::GetParent(h)));
				if (0 == caption.Find("Find Symbol") || 0 == caption.Find("Members of"))
					return PaintType::ListBox;
			}

			if (g_currentEdCnt && Is_C_CS_VB_File(g_currentEdCnt->m_ftype))
			{
				// edit controls in Create method, Create from usage, Extract
				// method and Rename dialogs are colored due to this block
				if (RefactoringActive::IsActive()) // [case: 84966]
					return PaintType::ListBox;
			}

			return PaintType::None;
		}
	}

	if (cls == "tooltips_class32")
	{
		CPoint screenPos;
		::GetCursorPos(&screenPos);
		HWND wnd = ::WindowFromPoint(screenPos);
		if (wnd && GetWindowClassString(wnd).contains("ToolbarWindow"))
			return FALSE;
		if (wnd && (((HINSTANCE)GetWindowLongPtr(wnd, GWLP_HINSTANCE)) == gVaDllApp->GetVaAddress()))
		{
			const WTString caption = GetWindowTextString(h);
			if (caption.GetLength() > 2 && caption[1] == ':' && (caption.Find(":\\") == 1 || caption.Find(":/") == 1))
				return FALSE; // don't color tooltips that are just file names

			if (gColorSyncMgr && gColorSyncMgr->GetActiveVsTheme() != ColorSyncManager::avtLight &&
			    gColorSyncMgr->GetActiveVsTheme() != ColorSyncManager::avtBlue)
			{
				// [case: 71740] don't color outline and vaview tooltips in vs2012 if light theme is not active
				return PaintType::None;
			}

			return PaintType::ToolTip;
		}
		return FALSE;
	}

	if (cls == "LiteTreeView32")
	{
		// vsnet class views
		int id = ::GetDlgCtrlID(h);
		WTString caption = GetWindowTextString(h);

		if (caption.contains("Class View") || caption.contains("ClassView"))
		{
			if (id != 0x100 && id != 0x200)
				return FALSE;
			return PaintType::View;
		}
		if (id == 0x200 || (id == 0x100 && !caption.contains("Resource")))
			return PaintType::ObjectBrowser;
		return FALSE;
	}

	if (cls == "VsCompletorPane")
	{
		if (g_currentEdCnt && Is_C_CS_VB_File(g_currentEdCnt->m_ftype))
			return PaintType::ListBox;
		return PaintType::None;
	}

	if (cls == "TREEGRID")
	{
		HWND h2 = ::GetParent(h);
		if (h2)
		{
			const WTString cls2 = GetWindowClassString(h2);
			if (cls2 == "DEBUGPANE")
			{
				// [case: 117348]
				mySetProp(h, "__VA_do_not_colour", (HANDLE)1);
				return PaintType::None;
			}
		}
	}

	if (inst != gVaDllApp->GetVaAddress() || cls == "SysTabControl32")
	{
		// [case: 21327] I don't understand why, but this check can not occur after
		// the call to GetWindowTextString on the next line...
		// if you add anything before this 'if', you need to test case 21327
		if (cls.Find("Tcx") != -1 || cls.Find("Taq") != -1 || cls.Find("Tdx") != -1)
			return PaintType::None;
		const WTString caption = GetWindowTextString(h);
		if (caption.Find(":\\") != -1 || caption.Find(":/") != -1 || caption.Find("File: ") != -1)
			return FALSE; // don't color file names

		int id = ::GetDlgCtrlID(h);
		if (inst != gVaDllApp->GetVaAddress() && cls == "Edit")
		{
			HINSTANCE parent_inst = (HINSTANCE)GetWindowLongPtr(::GetParent(h), GWLP_HINSTANCE);
			if (parent_inst != gVaDllApp->GetVaAddress()) // allow minihelp edits
				return FALSE;                             // resource editor VC6
			// this paint should be wrapped in ToolTipEditCombo.cpp
			// if we reach this point, it is a filename and should not be colored.
			return FALSE; // do not color filenames case: 12788
		}
		if (cls == "Afx:400000:8" && (id & 0xff00) != 0xe900)
			return PaintType::FindInFiles; // outputwindow/watch/callstack/...

		if (cls == "LiteTreeView32") // VANet solution window
		{
			int id2 = ::GetDlgCtrlID(h);
			if ((id2 != 0x100 && id2 != 0x200) // Object browser
			    && !caption.contains("Class View") && !caption.contains("ClassView"))
			{
				return FALSE;
			}
			return PaintType::View;
		}
		else if (cls == "Edit")
		{
			int id2 = ::GetDlgCtrlID(h);
			if (id2 != 0x66 && id2 != 0x64)
				return FALSE;
			return PaintType::WizardBar;
		}
		else if (cls != "Afx:400000:8" && cls != "tooltips_class32" && cls != "VsCompletorPane" && cls != "Edit"
		         /*&& cls != "ComboBox"*/)
		{
			if (caption != "ClassView")
				return FALSE;
			return PaintType::View;
		}

		if (g_paintingEdCtrl && (cls != "VsTextEditPane" && cls != "Afx:400000:8"))
			g_paintingEdCtrl = NULL; // Not our edit window painting.
		if (cls == "Afx:400000:8" && (!g_paintingEdCtrl && !g_printEdCnt))
			return FALSE; // painting in a non VA'd window
	}
	return PaintType::ObjectBrowser; // ?
}
extern HWND g_DefWindowProcHWND;

BOOL ShouldColorWindow(HDC dc)
{
	if (g_PaintLock || !Psettings || !Psettings->m_enableVA || !Psettings->m_ActiveSyntaxColoring)
		return FALSE;

	if (PaintType::in_WM_PAINT)
	{
		// In text/html/asp files only color "Identifiers" Case: 15381
		if (PaintType::SourceWindow == PaintType::in_WM_PAINT && g_currentEdCnt && g_currentEdCnt->m_txtFile &&
		    g_currentEdCnt->m_ftype != JS && GetTextColor(dc) != Psettings->m_colors[C_Text].c_fg)
			return FALSE;

		// This looks like it would make sense, but then coloring stops working.
		// Some (all?) places in VA rely on ShouldColorWindow to SetPaintType
		// even when paintType is DontColor.
		// 		if (PaintType::inPaintType == PaintType::DontColor)
		// 			return FALSE;

		return PaintType::SetPaintType(PaintType::in_WM_PAINT);
	}
	if (g_inPaint)
		return TRUE;

	static BOOL didColor = FALSE;
	static HDC ldc = NULL;
	static HWND lhWnd;
	static int sPrevPaintType = 0;
	if (g_inPaint)
		return TRUE;
	HWND h = ::WindowFromDC(dc);
	if (!h)
		h = g_DefWindowProcHWND;

	if (h && myGetProp(h, "__VA_do_not_colour"))
		return FALSE;

	if (dc == ldc && lhWnd == h)
	{
		if (didColor)
		{
			PaintType::SetPaintType(sPrevPaintType);
			return TRUE;
		}
		else
			return FALSE;
	}
	else
	{
		ldc = dc;
		didColor = FALSE;

		lhWnd = h;
		if (!h)
			return FALSE; // script

		sPrevPaintType = ShouldColorHWnd(h);
		if (!PaintType::SetPaintType(sPrevPaintType))
			return FALSE;
	}
	WTString cls = GetWindowClassString(h);
	if (cls == "Button" || cls == "SysHeader32" || cls == "Static")
		return FALSE;
	didColor = TRUE;
	return TRUE;
}

DWORD g_IgnoreBeepsTimer = 0;
BOOL(WINAPI* WTMessageBeepHookNext)(UINT nType) = NULL;
BOOL CALLBACK MessageBeepHook(UINT uType)
{
	extern BOOL g_DisplayFrozen;
	if (g_DisplayFrozen && g_mainThread == GetCurrentThreadId())
	{
		// Case 2153: If a dialog pops up when screen is frozen, VS2005 can lock up.
		// Unlock the screen if we get a beep because a dialog may pop up.
		FreezeDisplay _f; // unfreeze screen
	}
	if (g_IgnoreBeepsTimer && GetTickCount() < g_IgnoreBeepsTimer && (GetTickCount() + 2000) > g_IgnoreBeepsTimer)
		return TRUE;
	g_IgnoreBeepsTimer = 0;
	return WTMessageBeepHookNext(uType);
}

volatile bool override_save_filename = false;
std::wstring overridden_save_filename;
BOOL(APIENTRY* WTGetSaveFileNameWHookNext)(LPOPENFILENAMEW ofn) = nullptr;
BOOL APIENTRY GetSaveFileNameWHook(LPOPENFILENAMEW ofn)
{
	if (override_save_filename && !overridden_save_filename.empty())
	{
		if (ofn && ofn->lpstrFile)
		{
			if (ofn->nMaxFile >= (overridden_save_filename.size() + 1))
			{
				wcscpy_s(ofn->lpstrFile, ofn->nMaxFile, overridden_save_filename.c_str());
				overridden_save_filename.clear(); // once will be enough
				return TRUE;
			}
			else
			{
				// CommDlgExtendedError cannot be set
				return FALSE;
			}
		}
		else
			return FALSE;
	}
	return WTGetSaveFileNameWHookNext(ofn);
}
// use nullptr or L"" to disable
bool OverrideNextSaveFileNameDialog(const wchar_t* filename)
{
	if (filename && filename[0])
	{
		overridden_save_filename = filename;
		override_save_filename = true;
	}
	else
	{
		overridden_save_filename.clear();
		override_save_filename = false;
	}

	return !!WTGetSaveFileNameWHookNext;
}
// int (WINAPI *WTMessageBoxIndirectWHookNext)(CONST MSGBOXPARAMSW *lpmbp) = nullptr;
// int WINAPI
// MessageBoxIndirectWHook(CONST MSGBOXPARAMSW *lpmbp)
//{
//	if(override_save_filename)
//	{
//		if(lpmbp && ((lpmbp->dwStyle & 0xf) == MB_OK))
//			return IDOK;
//	}
//	return WTMessageBoxIndirectWHookNext(lpmbp);
//}

LRESULT(WINAPI* AfxCallWndProcNextHook)(CWnd* pWnd, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) = NULL;

// Added this to catch any exceptions in BG threads.
UINT(APIENTRY* _AfxThreadEntryHookNext)(void* pParam) = NULL;
UINT APIENTRY _AfxThreadEntryHook(void* pParam)
{
	//	_set_se_translator(OurTranslator);
	try
	{
		return _AfxThreadEntryHookNext(pParam);
	}
	catch (...)
	{
	}
	return TRUE;
}

extern ArgToolTip* g_pLastToolTip;

LRESULT(WINAPI* DispatchMessageAHookNext)(IN CONST MSG* lpMsg) = NULL;
extern "C"
#if !defined(RAD_STUDIO)
_declspec(dllexport) 
#endif
LRESULT WINAPI DispatchMessageAHook(IN MSG* lpMsg)
{
	if (!DispatchMessageAHookNext)
		return FALSE;

	LRESULT r = DispatchMessageAHookNext(lpMsg);

	try
	{
		if (lpMsg)
		{
			UINT nMsg = lpMsg->message;
			if (nMsg == WM_KILLFOCUS || nMsg == WM_KEYDOWN)
			{
				if (g_mainThread == GetCurrentThreadId())
				{
					if (g_pLastToolTip && g_pLastToolTip->IsWindowVisible() &&
					    (!g_currentEdCnt || g_currentEdCnt->m_ttParamInfo != g_pLastToolTip)) // catch stray tooltip
					{
						if (!((nMsg == WM_KEYDOWN) && (lpMsg->wParam == VK_CONTROL) &&
						      g_pLastToolTip->dont_close_on_ctrl_key))
							g_pLastToolTip->ShowWindow(SW_HIDE);
					}
				}
			}
		}
	}
	catch (...)
	{
		// 		WTString msg;
		// 		msg.Format("DMWH:%x, %x, %x ", lpMsg->message, lpMsg->wParam, lpMsg->wParam);
		// 		VALOGEXCEPTION(msg);
	}

	return r;
}

HFONT WINAPI OurCreateFontIndirectA(CONST LOGFONTA* lf)
{
	// msdev is creating a font, make sure our font matches theirs
	if (g_FontSettings && !g_printEdCnt && lf)
	{
		// dont set to printer font
		g_FontSettings->Update(FALSE);
	}
	return CreateFontIndirectA(lf);
}

extern int rapidfire;

LRESULT(WINAPI* PeekMessageWHookNext)
(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) = NULL;
extern "C" 
#if !defined(RAD_STUDIO)
_declspec(dllexport)
#endif
LRESULT WINAPI PeekMessageWHook(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	// This is to fix a bug in vs2010 B2, where their message loop forwards messages to MEF first,
	// then to our subclassed hwnd if not handled, preventing us from seeing wm_char's.
	LRESULT r = PeekMessageWHookNext(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
	if (!lpMsg)
		return r;

	static bool sEatChar = false;
	switch (lpMsg->message)
	{
	case WM_SYSKEYUP:
		// [case: 86270] in vs2010, we don't get WM_SYSKEYUP like in the earlier IDEs
		rapidfire = 0;
		break;
	case WM_KEYDOWN:
		if (VK_RETURN == lpMsg->wParam)
		{
			if (g_currentEdCnt && Psettings && Psettings->m_enableVA && WPF_ViewManager::Get())
			{
				if (!WPF_ViewManager::Get()->HasAggregateFocus())
					sEatChar = true;
				else if (Psettings->mUnrealEngineCppSupport && gShellAttr && gShellAttr->IsDevenv14OrHigher())
					g_currentEdCnt->SendMessage(WM_VA_UE_ENABLE_UMACRO_INDENT_FIX_IF_RELEVANT); // [case: 109205]
			}
		}
		else if (sEatChar)
			sEatChar = false;
		break;
	case WM_KEYUP:
		if (VK_RETURN == lpMsg->wParam)
		{
			if (sEatChar)
				sEatChar = false;
			else if (g_currentEdCnt)
				g_currentEdCnt->SendMessage(WM_VA_UE_DISABLE_UMACRO_INDENT_FIX_IF_ENABLED); // [case: 109205]
		}
		break;
	case WM_CHAR:
		// interesting doc on input routing:
		// https://github.com/jaredpar/VsVim/blob/master/Src/VsVim/KeyboardInputRouting.txt

		if (g_currentEdCnt && Psettings && Psettings->m_enableVA)
		{
			if (!(lpMsg->wParam & 0xffffff80) // Not multi byte chars from IME input [case=39165] and [case: 53114]
			    && (wRemoveMsg & PM_REMOVE) &&
			    !(wRemoveMsg & PM_NOYIELD)) // [case: 70780] alternate input on french keyboard problem
			{
				if (WPF_ViewManager::Get() && WPF_ViewManager::Get()->HasAggregateFocus())
				{
					if (lpMsg->wParam == VK_RETURN && gVaService && gVaService->GetOutlineFrame() &&
					    gVaService->GetOutlineFrame()->IsMsgTarget(lpMsg->hwnd))
					{
						// [case: 37846] keys pressed in outline
						vCatLog("Editor.Events", "VaEventUE PeekChar skipOut wp=%zx lp=%zx, h=%p r=%x", (uintptr_t)lpMsg->wParam,
						     (uintptr_t)lpMsg->lParam, hWnd, wRemoveMsg);
						break;
					}
					else if (gShellAttr && gShellAttr->IsDevenv15u8OrHigher() && g_currentEdCnt &&
					         g_currentEdCnt->HasBlockOrMultiSelection())
					{
						// [case: 117499]
						break;
					}

					if (sEatChar)
					{

						// [case: 103136]
						sEatChar = false;
						vCatLog("Editor.Events", "VaEventUE PeekChar eat wp=%zx lp=%zx, h=%p r=%x", (uintptr_t)lpMsg->wParam,
						     (uintptr_t)lpMsg->lParam, hWnd, wRemoveMsg);
					}
					else
					{
						// send directly to our EdCnt
						g_currentEdCnt->SendMessage(lpMsg->message, lpMsg->wParam, lpMsg->lParam);
						// Set this message to WM_NULL so it doesn't get typed twice.
						lpMsg->message = WM_NULL;
					}
				}
				else
					vCatLog("Editor.Events", "VaEventUE PeekChar NF wp=%zx lp=%zx, h=%p r=%x", (uintptr_t)lpMsg->wParam,
					     (uintptr_t)lpMsg->lParam, hWnd, wRemoveMsg);
			}
			else
				vCatLog("Editor.Events", "VaEventUE PeekChar skip wp=%zx lp=%zx, h=%p r=%x", (uintptr_t)lpMsg->wParam,
				     (uintptr_t)lpMsg->lParam, hWnd, wRemoveMsg);
		}
		break;
	case WM_UNICHAR:
		vCatLog("Editor.Events", "VaEventUE PeekChar UC wp=%zx lp=%zx, h=%p r=%x", (uintptr_t)lpMsg->wParam, (uintptr_t)lpMsg->lParam, hWnd,
		     wRemoveMsg);
		break;
	}

	return r;
}

#include "ParseThrd.h"
#define DO_REAL_PARSE_IMPL(VERSION, PARAMS, PARAMS_NAMES)                                                              \
	LRESULT(WINAPI* _DoRealParse##VERSION) PARAMS = NULL;                                                              \
	LRESULT WINAPI VA_DoRealParse##VERSION PARAMS                                                                      \
	{                                                                                                                  \
		/* use .ncb if Intellisense is preferred */                                                                    \
		if (!Psettings || Psettings->m_bUseDefaultIntellisense || !Psettings->m_enableVA)                              \
			return _DoRealParse##VERSION PARAMS_NAMES;                                                                 \
                                                                                                                       \
		static BOOL disableIntellisense = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "DefaultIntellisense") == "Disable";\
		static BOOL delayIntellisense = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "DefaultIntellisense") == "Delay";   \
		if (disableIntellisense)                                                                                       \
			return 0; /* Should LoadProject nuke NCB files with this set? */                                           \
		while (delayIntellisense && !gShellIsUnloading && g_ParserThread &&                                            \
		       g_ParserThread->IsNormalJobActiveOrPending())                                                           \
			Sleep(100); /* Delay until VA has finished parsing. */                                                     \
		if (gShellIsUnloading)                                                                                         \
			return 0;                                                                                                  \
		return _DoRealParse##VERSION PARAMS_NAMES;                                                                     \
	}
DO_REAL_PARSE_IMPL(28, (LPVOID a1, LPVOID a2, LPVOID a3, LPVOID a4, LPVOID a5, LPVOID a6, LPVOID a7),
                   (a1, a2, a3, a4, a5, a6, a7))
DO_REAL_PARSE_IMPL(24, (LPVOID a1, LPVOID a2, LPVOID a3, LPVOID a4, LPVOID a5, LPVOID a6), (a1, a2, a3, a4, a5, a6))

#if !defined(RAD_STUDIO)
bool HookFeacpDLL()
{
	// examples of feacp.dll position relative to the main exe:

	// C:\Code\DevStudio\Common\MSDev98\Bin\MSDEV.EXE
	// C:\Code\DevStudio\Common\MSDev98\Bin\FEACP.DLL

	// C:\Code\Microsoft eMbedded C++ 4.0\Common\EVC\Bin\Evc.exe
	// C:\Code\Microsoft eMbedded C++ 4.0\Common\EVC\Bin\FEACP.DLL

	// C:\Code\DevStudio7.1\Common7\IDE\devenv.exe
	// C:\Code\DevStudio7.1\Vc7\vcpackages\feacp.dll

	// C:\Code\DevStudio8\Common7\IDE\devenv.exe
	// C:\Code\DevStudio8\VC\vcpackages\feacp.dll

	// C:\Code\DevStudio9\Common7\IDE\devenv.exe
	// C:\Code\DevStudio9\VC\vcpackages\feacp.dll

	static BOOL hookIntellisense = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "DefaultIntellisense").GetLength() != 0;
	if (hookIntellisense)
	{
		static bool first = true;
		static char feacp_filename[512] = {0};
		if (first)
		{
			first = false;

			static const char* const locations[] = {
			    "feacp.dll",                          // vc6, evc4
			    "..\\..\\vc7\\vcpackages\\feacp.dll", // vs2003
			    "..\\..\\vc\\vcpackages\\feacp.dll"   // vs2005, vs2008
			};

			for (int i = 0; i < countof(locations); i++)
			{
				char path[512];
				path[0] = 0;
				GetModuleFileNameA(NULL, path, countof(path));
				::PathRemoveFileSpec(path);
				::PathAppend(path, locations[i]);
				if (::PathFileExists(path))
				{
					strcpy(feacp_filename, path);
					break;
				}
			}
		}
		if (!feacp_filename[0])
			return true;

		static HMODULE hFeacp_dll = LoadLibraryA(feacp_filename);
		if (hFeacp_dll)
		{
			if (!_DoRealParse28 && !_DoRealParse24) // VS2005, VS2008
			{
				FARPROC fp = GetProcAddress(hFeacp_dll, "_DoRealParse@28");
				if (fp)
				{
					WtHookCode(fp, VA_DoRealParse28, (PVOID*)&_DoRealParse28);
					return true;
				}
			}
			if (!_DoRealParse24 && !_DoRealParse28) // VS6, EVC4, VS2003
			{
				FARPROC fp = GetProcAddress(hFeacp_dll, "_DoRealParse@24");
				if (fp)
				{
					WtHookCode(fp, VA_DoRealParse24, (PVOID*)&_DoRealParse24);
					return true;
				}
			}
		}
	}

	return false;
}
#endif

// This is our hack solution to MSDev file save errors when Norton Anti-Virus
// is installed and realtime scanning is enabled.
// I used a file monitor and saw that when msdev saves a file it:
// - writes out a new file w/ temp name
// - deletes original file
// - moves temp file to original name
// Sometimes the move step fails with error code ERROR_DELETE_PENDING but
// the temp file gets deleted anyway.  The temp file is visible in the directory
// while MSDev is displaying the file save error dlg.
// Although the delete of the original file is successful, Norton must be doing
// something such that an attempt to create a file with the same name fails with
// ERROR_DELETE_PENDING.
// We address this by adding a Sleep after the DeleteFile call in order to give
// Norton a chance to complete its task.
BOOL WINAPI OurDeleteFileA(LPCSTR filename)
{
	const BOOL retval = DeleteFileA(filename);
	if (retval)
	{
		Sleep(100);
	}
	return retval;
}

#if !defined(RAD_STUDIO)
static HMODULE GetGdiMod()
{
	static HMODULE sGdi = NULL;
	if (!sGdi)
	{
		sGdi = GetModuleHandleA("gdi32.dll");
		if (!sGdi)
		{
			// [case: 39473] LoadLib and save handle so that we don't
			// constantly hit the slow FuckArm calls
			sGdi = LoadLibraryA("gdi32.dll");
			_ASSERTE(sGdi);
		}
	}

	return sGdi;
}
#endif

HMODULE
GetUsp10Module()
{
	static HMODULE sUspMod = NULL;
	if (!sUspMod)
	{
		const char* const kModName = "uSP10.dll";
		sUspMod = GetModuleHandleA(kModName);
		if (!sUspMod)
		{
			// [case: 39473] LoadLib and save handle so that we don't
			// constantly hit the slow FuckArm calls
			sUspMod = LoadLibraryA(kModName);
			_ASSERTE(sUspMod);
		}
	}
	return sUspMod;
}

#if !defined(RAD_STUDIO)
static HMODULE GetEditModule()
{
	static HMODULE sEditMod = NULL;
	if (!sEditMod)
	{
		if (gShellAttr->IsDevenv())
			sEditMod = GetUsp10Module();
		else
		{
			const char* kModName;
			if (gShellAttr->IsMsdev())
				kModName = "devedit.pkg";
			else
			{
				_ASSERTE(!"set EditModule name");
				return NULL;
			}

			sEditMod = GetModuleHandleA(kModName);
			if (!sEditMod)
			{
				// [case: 39473] LoadLib and save handle so that we don't
				// constantly hit the slow FuckArm calls
				sEditMod = LoadLibraryA(kModName);
				_ASSERTE(sEditMod);
			}
		}
	}

	return sEditMod;
}
#endif

#undef AfxWndProc

#ifdef _WIN64
// from GetCallstack64 in devenv.exe:
// do
//{
//	v12 = RtlLookupFunctionEntry(Rip, &ImageBase, 0i64);
//	if (v12)
//	{
//		HandlerData = 0i64;
//		EstablisherFrame[0] = 0i64;
//		EstablisherFrame[1] = 0i64;
//		RtlVirtualUnwind(0, ImageBase, ContextRecord->Rip, v12, ContextRecord, &HandlerData, EstablisherFrame, 0i64);
//		v13 = ContextRecord->Rip;
//		if (v13)
//		{
//			v14 = v9++;
//			if (v14 > a2)
//			{
//				if (a6)
//					*(_QWORD*)a6 += v13;
//				*(_QWORD*)(v19 + 8 * v11) = v13;
//				++v8;
//				if (++v11 == v20)
//					break;
//			}
//		}
//	}
//	Rip = ContextRecord->Rip;
//} while (Rip);

// RtlLookupFunctionEntry fails and loop becomes infinite as Rip will remain the same

namespace
{
bool IsGetCallstack64PatchingActive()
{
	return !!GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "InstallGetCallstack64Patch", 0);
}

CRITICAL_SECTION GetCallstack64Patch_cs;
ULONGLONG GetCallstack64Patch_ticks;
std::tuple<DWORD, DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE> GetCallstack64Patch_params;
uint32_t GetCallstack64Patch_counter;

constexpr uint32_t max_time = 1000; // 1 second
constexpr uint32_t needed_counter = 100;

RUNTIME_FUNCTION dummy_runtime_function;

using RtlLookupFunctionEntry_t = PRUNTIME_FUNCTION(NTAPI*)(DWORD64 ControlPc, PDWORD64 ImageBase,
                                                           PUNWIND_HISTORY_TABLE HistoryTable);
RtlLookupFunctionEntry_t RealRtlLookupFunctionEntry = nullptr;
PRUNTIME_FUNCTION NTAPI VARtlLookupFunctionEntry(DWORD64 ControlPc, PDWORD64 ImageBase,
                                                 PUNWIND_HISTORY_TABLE HistoryTable)
{
	auto ret = RealRtlLookupFunctionEntry(ControlPc, ImageBase, HistoryTable);

	if (!ret)
	{
		auto ticks = ::GetTickCount64();
		::EnterCriticalSection(&GetCallstack64Patch_cs);

		auto params = std::make_tuple(::GetThreadId(nullptr), ControlPc, ImageBase, HistoryTable);
		if (((ticks - GetCallstack64Patch_ticks) > max_time) || (params != GetCallstack64Patch_params))
		{
			// reset
			GetCallstack64Patch_params = params;
			GetCallstack64Patch_ticks = ticks;
			GetCallstack64Patch_counter = 0;
		}
		else
		{
			if (++GetCallstack64Patch_counter >= needed_counter)
			{
				// RtlLookupFunctionEntry has been called many times in a short period with same arguments; break the
				// infinite loop!
				GetCallstack64Patch_ticks = 0; // reset next time
				ret = &dummy_runtime_function; // simulate success to call RtlVirtualUnwind

				// ::OutputDebugString("FEC: bip");
			}
		}

		::LeaveCriticalSection(&GetCallstack64Patch_cs);
	}
	else
		GetCallstack64Patch_ticks = 0; // reset next time

	return ret;
};

using RtlVirtualUnwind_t = PEXCEPTION_ROUTINE(NTAPI*)(DWORD HandlerType, DWORD64 ImageBase, DWORD64 ControlPc,
                                                      PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord,
                                                      PVOID* HandlerData, PDWORD64 EstablisherFrame,
                                                      PKNONVOLATILE_CONTEXT_POINTERS ContextPointers);
RtlVirtualUnwind_t RealRtlVirtualUnwind = nullptr;
PEXCEPTION_ROUTINE NTAPI VARtlVirtualUnwind(DWORD HandlerType, DWORD64 ImageBase, DWORD64 ControlPc,
                                            PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord, PVOID* HandlerData,
                                            PDWORD64 EstablisherFrame, PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
	if (FunctionEntry == &dummy_runtime_function)
	{
		assert(ContextRecord);
		if (ContextRecord)
		{
#ifndef _ARM64
			ContextRecord->Rip = 0; // this is the condition to break the loop
#else
			ContextRecord->Pc = 0; // this is the condition to break the loop
#endif
		}

		// ::OutputDebugString("FEC: blip");
		return nullptr;
	}
	return RealRtlVirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry, ContextRecord, HandlerData,
	                            EstablisherFrame, ContextPointers);
}
} // namespace
#endif

void PatchW32Functions(bool patch)
{
#if defined(VAX_CODEGRAPH) || defined(RAD_STUDIO)
	return;
#else
	try
	{
		const DWORD doPatching = GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "InstallHooks", 1);
		if (!doPatching)
		{
			if (!patch)
				ReleaseColourizerHooks();
			return;
		}

		// intercept key win32 methods for ASC and font settings
		// NOTE: doesnt work while debugging in win98.

		static bool sPatched = false;
		if (!patch && !sPatched)
		{
			// unpatch can only occur once.
			// this check protects against calls from both FreeTheGlobals and LicenseInitFailed
			return;
		}

		if (patch)
		{
			sPatched = true;
			CatLog("LowLevel", "MadHook1");
			if (!AfxCallWndProcNextHook)
			{
				LRESULT AFXAPI AfxCallWndProc(CWnd*, HWND, UINT, WPARAM, LPARAM);

				InitializeMadCHook();
				AfxCallWndProcNextHook = AfxCallWndProc;
			}

			extern UINT APIENTRY _AfxThreadEntry(void* pParam);
			if (!_AfxThreadEntryHookNext)
				WtHookCode(_AfxThreadEntry, _AfxThreadEntryHook, (PVOID*)&_AfxThreadEntryHookNext);
			if (!DispatchMessageAHookNext)
				WtHookCode(DispatchMessageA, DispatchMessageAHook, (PVOID*)&DispatchMessageAHookNext);

			if (!PeekMessageWHookNext && gShellAttr->IsDevenv10OrHigher())
				WtHookCode(PeekMessageW, PeekMessageWHook, (PVOID*)&PeekMessageWHookNext);
			if (!WTMessageBeepHookNext)
				WtHookCode(MessageBeep, MessageBeepHook, (PVOID*)&WTMessageBeepHookNext);

			if (!WTGetSaveFileNameWHookNext)
				WtHookCode(::GetSaveFileNameW, GetSaveFileNameWHook, (PVOID*)&WTGetSaveFileNameWHookNext);
			//			if(!WTMessageBoxIndirectWHookNext)
			//				WtHookCode(::MessageBoxIndirectW, MessageBoxIndirectWHook, (PVOID
			//*)&WTMessageBoxIndirectWHookNext);

			CatLog("LowLevel", "MadHook1.2");

			if (gShellAttr->IsMsdev())
			{
				static bool onceCreateFont = true;
				static bool onceDeleteFile = true;
				HMODULE hMod = GetEditModule();
				if (onceCreateFont)
				{
					// trap font creation so we get same font
					HMODULE hGDI = GetGdiMod();
					FARPROC s_CreateFontIndirectA = GetProcAddress(hGDI, "CreateFontIndirectA");
					if (PatchFile(hMod, 0, s_CreateFontIndirectA, (FARPROC)(uintptr_t)OurCreateFontIndirectA))
						onceCreateFont = false;
				}

				if (hMod && onceDeleteFile)
				{
					if (PatchFile(hMod, 0, (FARPROC)(uintptr_t)DeleteFileA, (FARPROC)(uintptr_t)OurDeleteFileA))
						onceDeleteFile = false;
				}
			}

			if (!gShellAttr->IsDevenv10OrHigher())
			{
				static bool onceFeacp = true;
				if (onceFeacp)
					onceFeacp = !HookFeacpDLL();
			}

			PatchTextOutMethods(patch);

#ifdef _WIN64
			if (patch && IsGetCallstack64PatchingActive() && !RealRtlLookupFunctionEntry)
			{
				::InitializeCriticalSectionAndSpinCount(&GetCallstack64Patch_cs, 1024);
				::WtHookCode(::RtlLookupFunctionEntry, VARtlLookupFunctionEntry, (void**)&RealRtlLookupFunctionEntry);
				::WtHookCode(::RtlVirtualUnwind, VARtlVirtualUnwind, (void**)&RealRtlVirtualUnwind);
			}
#endif
		}
		else
		{
			sPatched = false;
			Log("MadHook2");

#ifdef _WIN64
			if (IsGetCallstack64PatchingActive())
			{
				::WtUnhookCode(VARtlVirtualUnwind, (void**) & RealRtlVirtualUnwind);
				RealRtlVirtualUnwind = nullptr;
				::WtUnhookCode(VARtlLookupFunctionEntry, (void**) & RealRtlLookupFunctionEntry);
				RealRtlLookupFunctionEntry = nullptr;
				::DeleteCriticalSection(&GetCallstack64Patch_cs);
			}
#endif

			ReleaseColourizerHooks();
			// Leave all patches in place, and do not allow the dll to unload.
			// fix crash on exit with language pack installed
			// PatchTextOutMethods(patch);
			Log("MadHook2.2");
			//			UnhookCode((PVOID*) &CoCreateInstanceExNextHook);
			FinalizeMadCHook();
		}
	}
	catch (...)
	{
		_ASSERTE(!"exception in PatchW32Functions");
	}
#endif
}
