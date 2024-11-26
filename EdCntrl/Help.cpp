#include "stdafxed.h"
#include "edcnt.h"
#include "resource.h"
#include <comdef.h>
#include "VaTimers.h"
#include "DevShellService.h"
#include "DevShellAttributes.h"
#include "PooledThreadBase.h"
#include "Directories.h"
#include "wt_stdlib.h"
#include "WindowUtils.h"
#include "Settings.h"
#include "WTComBSTR.h"
#include "Directories.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::string;
using OWL::TRegexp;

static WTString HelpSearchString;
static bool FindExactHelpMatch(LPCTSTR strIn, HWND helpSysList);
static IWebBrowserApp* g_pWBApp = NULL;

/*
LRESULT
TypeString(HWND h, WTString &str){
    WTString lstr = str;
    ::SendMessage(h, WM_SETREDRAW, false, 0);
    lstr.to_upper();
    int l = lstr.length();
    for(int i=0; i < l; i++){
        if(lstr[i] == '.')
            PostMessage(h, WM_KEYDOWN, (WPARAM) VK_DECIMAL, 0x001c0001);
        else
            PostMessage(h, WM_KEYDOWN, (WPARAM) lstr[i], 0x001c0001);
        // Sleep(100);
    }
    return ::SendMessage(h, WM_SETREDRAW, true, 0);
}
*/

bool DidHelpFlag = false;

class CAutoSelect : public PooledThreadBase
{
	WTString m_typestr;

  public:
	CAutoSelect(WTString str) : PooledThreadBase("CAutoSelect"), m_typestr(str)
	{
		StartThread();
	}

  private:
	virtual void Run()
	{
		try
		{
			HWND h;
			WTString clname;
			DWORD procID = GetCurrentProcessId();
			int x = gShellAttr->GetHelpOpenAttempts();
			for (; x; x--)
			{
				Sleep(200);
				HWND h2 = GetForegroundWindow();
				DWORD h2Proc, h2Thrd;
				h2Thrd = GetWindowThreadProcessId(h2, &h2Proc);
				// the window has to be from the current process.
				//  Fix crash of task manager if you click on it while
				//  this thread is running
				if (h2Proc != procID)
					continue;
				AttachThreadInput(GetCurrentThreadId(), h2Thrd, TRUE);
				h = ::GetFocus();
				AttachThreadInput(GetCurrentThreadId(), h2Thrd, FALSE);
				clname = GetWindowClassString(h);
				if (clname == "SysListView32")
				{
					FindExactHelpMatch(m_typestr.c_str(), h);
					break;
				}
				else if (clname == "Edit" && m_typestr[0] == ':')
				{
					if (gShellAttr->SupportsHelpEditClassName() || GetWindowClassString(GetParent(h)) == "HH Child")
						SendMessage(h, WM_SETTEXT, 0, (LPARAM)(LPCTSTR)&m_typestr.c_str()[1]);
					break;
				}
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("HLP:");
		}
	}
};

void LaunchUrl(WTString url)
{
	HRESULT hr;
	VARIANT vFlags = {0}, vTargetFrameName = {0}, vPostData = {0}, vHeaders = {0};
	IDispatch* pDisp = NULL; // used to confirm that browser is still open
	if (g_pWBApp)
		g_pWBApp->get_Application(&pDisp);
	if (!g_pWBApp || (g_pWBApp && !pDisp))
	{
		if (g_pWBApp)
			g_pWBApp->Release();
		if (FAILED(hr = CoCreateInstance(CLSID_InternetExplorer, NULL, CLSCTX_SERVER, IID_IWebBrowserApp,
		                                 (LPVOID*)&g_pWBApp)))
		{
			return;
		}
	}
	WTComBSTR bStrURL;
	hr = g_pWBApp->Navigate(bStrURL, &vFlags, &vTargetFrameName, &vPostData, &vHeaders);
	bStrURL = url.Wide();
	if (!bStrURL.Length())
		return;
	hr = g_pWBApp->Navigate(bStrURL, &vFlags, &vTargetFrameName, &vPostData, &vHeaders);
	hr = g_pWBApp->put_Visible(VARIANT_TRUE);
}

int EdCnt::Help()
{
	long p1 = 0, p2;
	int retVal = 0;
	bool changedSel = false;
	token t;
	WTString cw;
	if (m_ReparseScreen)
	{
		// file needs to be flushed
		m_ReparseScreen = false;
		// ::PostMessage(MainWndH, WM_COMMAND, 0xe146, 0);
		// timer will repost message
		SetTimer(ID_HELP_TIMER, 300, NULL);
		DidHelpFlag = true;
		return TRUE;
	}
VERIFY_HELP:
	DidHelpFlag = false;
	DType dt2(GetSymDtype());
	if (!Psettings->oneStepHelp || Scope()[0] != ':' || !dt2.type())
		goto EXIT_HELP;

	if (!dt2.inproject() && !dt2.IsDbSolutionPrivateSystem())
	{ // let them do help...
		// Get context for results list
		token t2 = GetSymScope();
		HelpSearchString = t2.read(":");
		// use to pick foobar.member when we wanted foo.member
		if (t2.more())
		{ // added "." to scope of "foo."
			HelpSearchString += "::";
			HelpSearchString += t2.read(":");
		}
		new CAutoSelect(HelpSearchString);
		HelpSearchString.Empty();
		retVal = 1;
		goto EXIT_HELP;
	}

	if (dt2.type() == FUNC)
	{
		// local method
		// ::TODO Search for overriding method
		HelpSearchString = ""; // should be scope of overriden method
		retVal = 1;
		goto EXIT_HELP;
	}
	// Local var, prompt for help on definition
	t = string(GetSymDef().c_str());
	cw = CurWord();
	if (strchr("!>.(& \t\r\n{:=+-*,\"/", cw.GetAt(cw.GetLength() - 1)))
	{
		GetSel(p1, p2);
		if (p1 == p2 && !changedSel)
		{
			SetSelection(p1 + 1, p1 + 1);
			CurScopeWord();
			changedSel = true;
			goto VERIFY_HELP;
		}
	}
	while (t.more())
	{
		WTString Sym = t.read(" *&\t:[]()=");
		if (Sym == cw)
		{
			retVal = 1;
			break;
		}
		WTString sym = WTString(":") + Sym;
		WTString def;
		int type, attrs;
		type = attrs = 0;
		MultiParsePtr mp(GetParseDb());
		mp->Tdef(sym, def, TRUE, type, attrs);
		DType dt;
		dt.setType((uint)type, (uint)attrs, 0);
		if (dt.type() && !dt.inproject() && !dt.IsDbSolutionPrivateSystem())
		{
			WTString title;
			WTString msg, frmt;
			frmt.LoadString(IDS_HELP_ON_VAR);
			msg.__WTFormat(0, frmt.c_str(), cw.c_str(), GetSymDef().c_str(), Sym.c_str());
			frmt.LoadString(IDS_HELP_TITLE);
			title.__WTFormat(0, frmt.c_str(), Sym.c_str());
			HelpSearchString = "";
			DidHelpFlag = true;
			if (WtMessageBox(GetSafeHwnd(), msg, title, MB_OKCANCEL) == IDOK)
			{
				if (::GetFocus() != (HWND) * this)
					::SetFocus(*this);
				new CAutoSelect(sym);
				HelpSearchString.Empty();
				gShellSvc->HelpSearch();
			}
			retVal = 1;
			break;
		}
	}
EXIT_HELP:
	if (changedSel)
		SetSelection(p1, p1);
	return retVal;
}

void CleanupHelp()
{
	if (g_pWBApp)
	{
		g_pWBApp->Quit();
		g_pWBApp->Release();
		g_pWBApp = NULL;
	}
}

bool FindExactHelpMatch(LPCTSTR strIn, HWND helpSysList)
{
	LVFINDINFO fItem;
	fItem.flags = LVFI_STRING;
	fItem.psz = LPCTSTR(strIn);

	int pos = ListView_FindItem(helpSysList, -1, &fItem);
	if (pos == -1)
	{
		// if strIn is SendMessageA, it won't show in the results list
		static token t;
		t = strIn;
		if (Psettings->m_AsciiW32API)
			t.ReplaceAll(TRegexp("A$"), string(""));
		else
			t.ReplaceAll(TRegexp("W$"), string(""));
		fItem.psz = t.c_str();
		pos = ListView_FindItem(helpSysList, -1, &fItem);
	}
	// now get the text at pos and do a case-sensitive compare
	// if searching for BOOL don't pick bool
	while (pos != -1)
	{
		char txt[MAX_PATH] = "";
		ListView_GetItemText(helpSysList, pos, 0, txt, MAX_PATH);
		if (strcmp(txt, fItem.psz))
		{
			// if no match search again
			pos = ListView_FindItem(helpSysList, pos, &fItem);
			if (!IsWindow(helpSysList))
			{
				// Listview_FindItem could return could return 0 if
				//  the window handle is no longer valid
				pos = -1;
				break;
			}
		}
		else
			break;
	}
	if (pos != -1)
	{
		// we've got a direct hit - it might not be the only one.
		// we could use the next statements and make them optional,
		//   so that we select if there's only one match.
		//		int pos2 = ListView_FindItem(helpSysList, pos, &fItem);
		//		if (pos2 == -1) { // it's an only hit
		if (!ListView_EnsureVisible(helpSysList, pos, FALSE))
			return false;
		POINT pt;
		if (!ListView_GetItemPosition(helpSysList, pos, &pt))
			return false;
		pt.x++;
		pt.y++;
		PostMessage(helpSysList, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
		PostMessage(helpSysList, WM_LBUTTONUP, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
		HWND hp = ::GetParent(::GetParent(::GetParent(helpSysList)));
		::PostMessage(helpSysList, WM_KEYDOWN, VK_RETURN, 0x001c0001);

		WTString pname = GetWindowTextString(hp);
		if (pname == "Results List")
		{ // VC5 only
			// the results list is an undocked window - safe to close hp
			::PostMessage(hp, WM_CLOSE, 0, 0);
			//  we can't close the results list if it's docked
			//      closing hp while results list is docked and then
			//      selecting View->Results List will cause a crash.
			//      same with ::GetParent(::GetParent(h))
		}
		return true;
		//		}
	}
	return false;
}
