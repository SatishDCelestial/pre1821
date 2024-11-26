#include "stdafxed.h"
#include <afxpriv.h>
#include "resource.h"
#include "VADialog.h"
#include "Registry.h"
#include "RegKeys.h"
#include "DevShellAttributes.h"
#include "EdCnt.h"
#include "PROJECT.H"
#include "VaService.h"
#include "uilocale.h"
#include "IdeSettings.h"
#include "VaAfxW.h"
#include "utils_goran.h"
#include "WindowUtils.h"

#include <thread>
#include <chrono>
#include "..\common\ScopedIncrement.h"
#include "DebugStream.h"

#ifdef RAD_STUDIO
#include "CppBuilder.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

const UINT WM_VA_INVOKE = ::RegisterWindowMessageA("WM_VA_INVOKE");

VADialog* VADialog::sActiveVaDlg = nullptr;
CStringW VADialog::sActiveVaDlgCaption;

IMPLEMENT_DYNAMIC(VADialog, cdxCDynamicDialog);

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VADialog, cdxCDynamicDialog)
ON_WM_HELPINFO()
ON_WM_SYSCOMMAND()
END_MESSAGE_MAP()
#pragma warning(pop)

VADialog::VADialog(UINT idd, CWnd* pParent, Freedom fd, UINT nFlags)
    : cdxCDynamicDialog(idd, pParent ? pParent : (gShellAttr && (gShellAttr->IsDevenv10OrHigher() || gShellAttr->IsCppBuilder()) ? gMainWnd : NULL), fd,
                        nFlags),
      mRepositionToCaret(FALSE)
{
	sActiveVaDlg = this;
	SetFontType(VaFontType::EnvironmentFont);
}

VADialog::VADialog(LPCTSTR lpszTemplateName, CWnd* pParent, Freedom fd, UINT nFlags)
    : cdxCDynamicDialog(lpszTemplateName,
                        pParent ? pParent : (gShellAttr && (gShellAttr->IsDevenv10OrHigher() || gShellAttr->IsCppBuilder()) ? gMainWnd : NULL), fd,
                        nFlags),
      mRepositionToCaret(FALSE)
{
	sActiveVaDlg = this;
	SetFontType(VaFontType::EnvironmentFont);
}

VADialog::~VADialog()
{
	if (sActiveVaDlg == this)
	{
		sActiveVaDlg = nullptr;
		sActiveVaDlgCaption.Empty();
	}
}

// void VADialog::OnDpiChanged(DpiChange change, bool& handled)
// {
// 	const MSG* msg = GetCurrentMessage();
//
// 	if (change != CDpiAware::DpiChange::Parent && msg)
// 		DoOnDPIChange(msg->message, msg->wParam, msg->lParam);
//
// 	if (change == CDpiAware::DpiChange::Parent)
// 	{
// 		if (msg && msg->message == WM_DPICHANGED && msg->lParam)
// 		{
// 			// TRY PREVENT LAYOUT, DefWindowProc and LOAD
//
// 			CRect suggested((LPCRECT)msg->lParam);
// 			CRect curr;
// 			GetWindowRect(&curr);
//
// 			double scaleX = (double)suggested.Width() / curr.Width();
// 			double scaleY = (double)suggested.Height() / curr.Height();
//
// 			VADEBUGPRINT(std::setprecision(2) << std::fixed << "#DPI VADialog::OnDpiChanged scale: " << scaleX << " " <<
// scaleY);
//
//
// 			DoOnDPIChange(msg->message, msg->wParam, msg->lParam);
//
// 			//VADEBUGPRINT("#DPI VADialog::OnDpiChanged - MoveWindow");
//
// 			//MoveWindow((LPCRECT)msg->lParam);
//
// 			DefWindowProc(msg->message, msg->wParam, msg->lParam);
//
// 			//Invoke(false, std::bind(&cdxCDynamicWnd::Layout, this));
// 		}
// 	}
//
// 	handled = true;
// }

static void PV_GetSystemIconFont(CStringW& strFontName, int& nPointSize)
{
	LOGFONTW lf;

	// get LOGFONT structure for the icon font
	SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(LOGFONTW), &lf, 0);

	// getting number of pixels per logical inch
	// along the display height
	HDC hDC = ::GetDC(NULL);
	int nLPixY = GetDeviceCaps(hDC, LOGPIXELSY);
	::ReleaseDC(NULL, hDC);

	// copy font parameters
	nPointSize = -MulDiv(lf.lfHeight, 72, nLPixY);
	strFontName = lf.lfFaceName;
}

static void PV_AdjustDialogTemplateFont(CDialogTemplateW& dlt)
{
	enum DialogFontBehavior
	{
		useIconFontFaceAndSize, // match vs2003+ dialog behavior (font face based on icon font; dialog size scales with
		                        // icon font size) (default)
		useIconFontFaceNotSize, // match vs2003+ dialog font face but size per resource template
		useResTemplateFonts,    // standard dialog behavior (OS-dependent face, constant size)
		dfbCount
	};
	DialogFontBehavior dfb =
	    (DialogFontBehavior)::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "DlgFontStyle", useIconFontFaceAndSize);
	if (dfb >= dfbCount || dfb < 0)
		dfb = useIconFontFaceAndSize;

	if (useResTemplateFonts == dfb)
		return;

	CStringW strFontName;
	int nPointSize = 0;

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
	{
		if (gVaInteropService)
		{
			// works for vs2010+
			variant_t ff, fs;
			if (gVaInteropService->TryFindResource(L"VsFont.EnvironmentFontFamily", L"string", &ff) &&
			    gVaInteropService->TryFindResource(L"VsFont.EnvironmentFontSize", L"double", &fs))
			{
				nPointSize = WindowScaler::Round<int>(fs.dblVal * (72.0 / 96.0));
				strFontName = ff.bstrVal;
			}
		}

		// if failed (which should not), try calculate from pixels
		if (strFontName.IsEmpty() && g_IdeSettings)
		{
			LOGFONTW lf;
			ZeroMemory((PVOID)&lf, sizeof(LOGFONTW));
			if (g_IdeSettings->GetEnvironmentFont(&lf))
			{
				if (lf.lfFaceName[0] && lf.lfHeight)
				{
					strFontName = lf.lfFaceName;
					if (lf.lfHeight > 0)
						nPointSize = lf.lfHeight;
					else
					{
						HDC hdc = ::GetDC(NULL);
						if (hdc)
						{
							nPointSize =
							    WindowScaler::Round<int>(-lf.lfHeight * 72.0 / ::GetDeviceCaps(hdc, LOGPIXELSY));
							::ReleaseDC(NULL, hdc);
						}
					}
				}
			}
		}
	}

	if (!nPointSize || !strFontName.GetLength() || !strFontName[0])
		PV_GetSystemIconFont(strFontName, nPointSize);

	if (useIconFontFaceNotSize == dfb)
	{
		// retain size set in resource template
		CString oldFontName;
		WORD oldPointSize;
		dlt.GetFont(oldFontName, oldPointSize);
		nPointSize = oldPointSize;
	}

	// 	if(gShellAttr && gShellAttr->IsDevenv10OrHigher() && gPkgServiceProvider)
	// 	{
	// 		IUIHostLocale2 *hl2 = NULL;
	// 		if(SUCCEEDED(gPkgServiceProvider->QueryService(SID_SUIHostLocale, IID_IUIHostLocale2, (void **)&hl2)) &&
	// hl2)
	// 		{
	// 			hl2->MungeDialogFont(...);
	// 			hl2->Release();
	// 		}
	// 	}

	vCatLog("LowLevel", "VADialog font %s", (LPCTSTR)CString(strFontName));
	dlt.SetFont(strFontName, (WORD)nPointSize);
}

INT_PTR
VADialog::DoModal()
{
	// [case: 142236]
	VsUI::CDpiScope dpi(true);

	CDialogTemplateW dlt;

	// load dialog template
	if (!dlt.Load(m_lpszTemplateName))
		return -1;

	ScopedIncrement si(&gExecActive);
	PV_AdjustDialogTemplateFont(dlt);

	// get pointer to the modified dialog template
	LPSTR pdata = (LPSTR)GlobalLock(dlt.m_hTemplate);

	// let MFC know that you are using your own template
	m_lpszTemplateName = NULL;
	InitModalIndirect(pdata);

	// display dialog box
	INT_PTR nResult = __super::DoModal();

	// unlock memory object
	GlobalUnlock(dlt.m_hTemplate);

	return nResult;
}

BOOL VADialog::Create(LPCTSTR lpszTemplateName, CWnd* pParentWnd)
{
	CDialogTemplateW dlt;

	//	_ASSERTE(m_lpszTemplateName == NULL);
	if (!dlt.Load(lpszTemplateName))
		return FALSE;

	PV_AdjustDialogTemplateFont(dlt);

	HINSTANCE hInst = AfxFindResourceHandle(lpszTemplateName, RT_DIALOG);

	if (gShellAttr && gShellAttr->IsDevenv10OrHigher() && pParentWnd)
		m_pParentWnd = pParentWnd;
	return __super::CreateIndirect(dlt.m_hTemplate, pParentWnd, hInst);
}

BOOL VADialog::Create(UINT nIDTemplate, CWnd* pParentWnd)
{
	return Create(MAKEINTRESOURCE(nIDTemplate), pParentWnd);
}

BOOL VADialog::OnInitDialog()
{
	BOOL bOK = __super::OnInitDialog();
	HICON hTomato = LoadIcon(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_TOMATO));
	SetIcon(hTomato, TRUE);
	SetIcon(hTomato, FALSE);
	::GetWindowTextW(m_hWnd, sActiveVaDlgCaption);
#if defined(RAD_STUDIO)
	// #cppbHostTODO host needs to implement IRadStudioHost::LaunchHelp so we can call it; 
	// Supported on VA side via the host proxy; in the meantime, remove the help button from dialogs
	ModifyStyleEx(WS_EX_CONTEXTHELP, 0, 0);
#endif
	return bOK;
}

void
VADialog::LaunchHelp()
{
	if (mHelpUrl.length())
	{
#if defined(RAD_STUDIO)
		if (gRadStudioHost)
			gRadStudioHost->ExLaunchHelp(mHelpUrl.Wide());
#else
		::ShellExecuteW(NULL, L"open", mHelpUrl.Wide(), NULL, NULL, SW_SHOW);
#endif
	}
}

void VADialog::OnSysCommand(UINT msg, LPARAM lParam)
{
	if (msg == SC_CONTEXTHELP)
		LaunchHelp();
	else
		__super::OnSysCommand(msg, lParam);
}

BOOL VADialog::OnHelpInfo(HELPINFO* info)
{
	LaunchHelp();
	return TRUE;
}

void VADialog::SetHelpTopic(WTString helpTopic)
{
	// NOTE: Update Snippets Editor help link in VATempDlg.cpp
	_ASSERTE(!helpTopic.IsEmpty());
#if defined(RAD_STUDIO)
	// RadStudio will redirect
	_ASSERTE(helpTopic.Find("http") != 0);
	mHelpUrl = helpTopic;
#else
	if (helpTopic.Find("http") == 0)
		mHelpUrl = helpTopic;
	else
	{
		const WTString url = "http://www.wholetomato.com/support/tooltip.asp?option=";
		mHelpUrl = url + helpTopic;
	}
#endif
}

void VADialog::OnInitialized()
{
	__super::OnInitialized();

	if (mRepositionToCaret && g_currentEdCnt)
		g_currentEdCnt->PositionDialogAtCaretWord(this);
}

void VADialog::CreateUnicodeEditControl(int dlgId, const CStringW& txt, CEdit& ctrl, bool subclass /*= false*/)
{
	CEdit* pOldEd = (CEdit*)GetDlgItem(dlgId);
	_ASSERTE(pOldEd);
	if (pOldEd)
	{
		// http://stackoverflow.com/a/1323192/103912
		// SetWindowTextW works fine with unicode text and an ansi EDIT,
		// but GetWindowTextW doesn't - it returns questions marks for unicode
		// chars.
		// Replace the ansi EDIT with a unicode EDIT.
		CRect rc;
		pOldEd->GetWindowRect(&rc);
		ScreenToClient(rc);
		DWORD style = pOldEd->GetStyle();
		DWORD exStyle = pOldEd->GetExStyle();
		CFont* pFont = pOldEd->GetFont();
		HWND hEdit = nullptr;
		for (uint cnt = 0; cnt < 10; ++cnt)
		{
			hEdit = ::CreateWindowExW(exStyle, L"EDIT", txt, style, rc.left, rc.top, rc.Width(), rc.Height(),
			                          GetSafeHwnd(), (HMENU)(intptr_t)dlgId, 0, 0);

			if (hEdit != nullptr)
				break;

			// add retry for [case: 111864]
			DWORD err = GetLastError();
			vLog("WARN: CreateUnicodeEditControl call to CreateWindowEx failed, 0x%08lx\n", err);
			Sleep(100 + (cnt * 50));
		}

		if (subclass)
			ctrl.SubclassWindow(hEdit);
		else
			ctrl.Attach(hEdit);

		ctrl.SetFont(pFont);

		pOldEd->CloseWindow();
		pOldEd->DestroyWindow();
	}
}

// If 1, uses standard behavior, as for example in VS Editor.
// Else uses behavior where whitespace after identifier is not
// skipped/selected in one step.
#define USE_STANDARD_BEHAVIOR 1

enum CClass
{
	CC_Punctuation,
	CC_Identifier,
	CC_WhiteSpace
};

static CClass CharClass(WCHAR wch)
{
	if (wch == '$' || wch == '_' || iswalnum(wch))
		return CC_Identifier;

	if (iswspace(wch))
		return CC_WhiteSpace;

	return CC_Punctuation;
}

// returns length of move for current punctuation context
// This version of function always scans in right direction!!!
// Look on how it is used in WordBreakProc in WB_LEFT section
static INT GetPuncRightMoveLength(LPWSTR s, INT index, INT len)
{
	// 2ch operators (all C++ and C# together)
	static LPCWSTR ch2_ops[] = {
	    L"==", L"!=", L"++", L"--", L"->", L"::", L"||", L"&&", L"-=", L"+=",
	    L">>", L"<<", L">=", L"<=", L"&=", L"|=", L"*=", L"/=", L"^=", L"%=",

	    L"=>", L"??", L"?.", // C# specific

	    NULL // delimiter
	};

	// 3ch long operators
	static LPCWSTR ch3_ops[] = {
	    L"<<=", L">>=",

	    NULL // delimiter
	};

	WCHAR txt[4];    // buffer for operator from text
	int txt_len = 0; // length of taken text

	// read text direction
	while (txt_len < 3 && index + txt_len < len && CC_Punctuation == CharClass(s[index + txt_len]))
	{
		txt[txt_len] = s[index + txt_len];
		txt_len++;
	}

	txt[txt_len] = '\0';

	// default return value for punctuation
	int retval = 1;

	switch (txt_len)
	{
		// compare taken text with all 2ch operators,
		// because current text is also 2ch long
	case 2:
		for (int i = 0; ch2_ops[i]; i++)
		{
			if (wcsncmp(ch2_ops[i], txt, 2) == 0)
			{
				retval = 2;
				break;
			}
		}
		break;

	case 3:
		// compare taken text with all 3ch operators
		// and then 2ch, because current text is 3ch long
		for (int i = 0; ch3_ops[i]; i++)
		{
			if (wcsncmp(ch3_ops[i], txt, 3) == 0)
			{
				retval = 3;
				break;
			}
		}

		// if 3ch operators do not match,
		// test also 2ch operators
		if (retval == 1)
		{
			for (int i = 0; ch2_ops[i]; i++)
			{
				if (wcsncmp(ch2_ops[i], txt, 2) == 0)
				{
					retval = 2;
					break;
				}
			}
		}
		break;
	}

	return retval;
}

int CALLBACK WordBreakProc(LPWSTR s, INT index, INT len, INT code)
{
	static DWORD ticks = 0;     // ticks count at last RIGHT move
	static INT last_index = -1; // last RIGHT move index
	static HWND hWhd = NULL;    // last focus

	// This modification allows us to use single
	// callback method for multiple edit controls
	HWND focus = GetFocus();
	if (focus != hWhd)
	{
		ticks = 0;
		last_index = -1;
		hWhd = focus;
	}

	switch (code)
	{
	case WB_ISDELIMITER:
		// Always return TRUE, so BUG does not cause modification
		// of starting index. If I return FALSE, and user wants move
		// to right, BUG causes increase of index, which means calculation
		// issues and word break can not be correctly identified.
		return TRUE;

	case WB_LEFT:
		if (index > 0)
		{
			// get current character class before caret
			CClass cc = CharClass(s[index - 1]);

			// if punctuation char is before caret, we need to check if
			// there are any operators in text, so we must move left until
			// char class differs from CC_Punctuation and then move right
			// until we find correct new caret position
			if (cc == CC_Punctuation)
			{
				int tmp_id = index;

				// move backward until char class differs
				while (tmp_id > 0 && cc == CharClass(s[tmp_id - 1]))
					tmp_id--;

				// find correct new index
				// Note: operators in chain are always considered in left to right order
				while (tmp_id < len && tmp_id < index)
				{
					// move in left to right order to find best operator end
					int move = GetPuncRightMoveLength(s, tmp_id, len);

					// while move + tmp_id is less then index, we need to check,
					// if there is another possible correct caret position,
					// in not, set index to previous position, which is our LEFT move.
					if (move + tmp_id < index)
						tmp_id += move;
					else
					{
						index = tmp_id;
						break;
					}
				}
			}
			else
			{
				// else move backward until char class differs
				while (index > 0 && cc == CharClass(s[index - 1]))
					index--;
			}

#if defined(USE_STANDARD_BEHAVIOR) && (USE_STANDARD_BEHAVIOR == 1)
			// if previous move was not over whitespace
			// to apply standard behavior, skip whitespace
			if (cc != CC_WhiteSpace)
				while (index > 0 && CC_WhiteSpace == CharClass(s[index - 1]))
					index--;
#endif
		}

		// return found index
		return index;

	case WB_RIGHT:
		// BUG in this callback causes that this part is invoked twice
		// and second call also increases index from previous call by 1.
		// To workaround it and preserve compatibility for case, when it
		// is not invoked twice, we will assume, that user is not
		// terminator able to press key twice between 2 ticks :)
		if (GetTickCount() - ticks < 1)
			return (last_index < 0) ? index : last_index;

		if (index < len)
		{
			// get current character class on caret's position
			CClass cc = CharClass(s[index]);

			// if punctuation char is on caret's position,
			// use GetPuncRightMoveLength to get move length
			if (cc == CC_Punctuation)
				index += GetPuncRightMoveLength(s, index, len);
			else
			{
				// else move forward until char class differs
				while (index < len && cc == CharClass(s[index]))
					index++;
			}

#if defined(USE_STANDARD_BEHAVIOR) && (USE_STANDARD_BEHAVIOR == 1)
			// if previous move was not over whitespace
			// to apply standard behavior, skip whitespace
			if (cc != CC_WhiteSpace)
				while (index < len && CC_WhiteSpace == CharClass(s[index]))
					index++;
#endif
		}

		// save current ticks count
		ticks = GetTickCount();

		// return found index
		return last_index = index;
	}

	return 0;
}

void VADialog::UpdateWordBreakProc(int dlgId)
{
	::SendDlgItemMessageW(m_hWnd, dlgId, EM_SETWORDBREAKPROC, 0, (LPARAM)(EDITWORDBREAKPROCW)WordBreakProc);
}

LRESULT
VADialog::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_VA_INVOKE)
	{
		__lock(mToInvokeCS);
		std::map<WPARAM, std::function<void(void)>>::iterator it = mToInvoke.find(wParam);
		if (it != mToInvoke.end())
		{
			it->second();
			mToInvoke.erase(it);
		}
		return TRUE;
	}

	LRESULT result = 0;
	if (HandleWndMessage(message, wParam, lParam, result))
		return result;

	return __super::WindowProc(message, wParam, lParam);
}

ULONG
VADialog::GetGestureStatus(CPoint ptTouch)
{
	// [case: 111020]
	// https://support.microsoft.com/en-us/help/2846829/how-to-enable-tablet-press-and-hold-gesture-in-mfc-application
	// https://connect.microsoft.com/VisualStudio/feedback/details/699523/tablet-pc-right-click-action-cannot-invoke-mfc-popup-menu
	return 0;
}

UINT_PTR
VADialog::Invoke(bool synchronous, std::function<void(void)> fnc)
{
	UINT_PTR invokeId;

	{
		__lock(mToInvokeCS);
		invokeId = mNextInvokeId++;
		mToInvoke[invokeId] = fnc;
	}

	if (synchronous)
		SendMessage(WM_VA_INVOKE, invokeId, 0);
	else
		PostMessage(WM_VA_INVOKE, invokeId, 0);

	return invokeId;
}

UINT_PTR
VADialog::DelayedInvoke(unsigned int delay_ms, std::function<void(void)> fnc)
{
	if (delay_ms == 0)
		return Invoke(false, fnc);
	else
	{
		HWND hwnd = m_hWnd;
		UINT_PTR invokeId;

		{
			__lock(mToInvokeCS);
			invokeId = mNextInvokeId++;
			mToInvoke[invokeId] = fnc;
		}

		try
		{
			std::thread([hwnd, invokeId, delay_ms]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

				if (::IsWindow(hwnd))
					::PostMessage(hwnd, WM_VA_INVOKE, invokeId, 0);
			}).detach();
		}
		catch (const std::exception&)
		{
			vLog("ERROR: VADialog::DelayedInvoke exception caught");
			try
			{
				return Invoke(false, fnc);
			}
			catch (const std::exception&)
			{
				vLog("ERROR: VADialog::DelayedInvoke exception caught (2)");
				return 0;
			}
		}

		return invokeId;
	}
}

BOOL VADialog::RemoveInvoke(UINT_PTR invoke_id)
{
	__lock(mToInvokeCS);
	std::map<UINT_PTR, std::function<void(void)>>::iterator it = mToInvoke.find(invoke_id);
	if (it != mToInvoke.end())
	{
		mToInvoke.erase(it);
		return TRUE;
	}
	return FALSE;
}

void VADialog::ClearInvokes()
{
	__lock(mToInvokeCS);
	mToInvoke.clear();
}
