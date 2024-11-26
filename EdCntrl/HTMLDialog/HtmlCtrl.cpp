////////////////////////////////////////////////////////////////
//   2003 Shilonosov Alexander. 
//   This is CHtmlView control costumized by Paul DiLascia,
//	 with my changes.
//
//   SOme code was taken from "Ehsan Akhgari" samples
// 
// - ParentWnd should be inherited from CHtml_Host_Handlers
//
//
//
//
//

////////////////////////////////////////////////////////////////
// Microsoft Systems Journal -- December 1999
// If this code works, it was written by Paul DiLascia.
// If not, I don't know who wrote it.
// Compiles with Visual C++ 6.0, runs on Windows 98 and probably NT too.
//
// ---
// AboutHtml shows how to implement an HTML About Dialog using a
// new class, CHtmlCtrl, that lets you use CHtmlView as a control in a dialog.
//
//
//

#include "stdafxed.h"
#include "HtmlCtrl.h"
#include "Html_Host_Handlers.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MFC_VER >= 0x0700
#include <afxocc.h>
#else
#include <..\src\occimpl.h>
#endif

#include "DocHostSite.h"
#include "HtmlDialog.h"


CHtmlCtrl::CHtmlCtrl() : 
	CHtmlView(), 
	CHtmlScript(),
	m_pOccManager(NULL),
	m_bCtl_mode(false)
{
#if _MSC_VER >= 1400
	m_pOccDialogInfo = NULL;
#endif
}

#pragma warning(push)
#pragma warning(disable: 4191)
IMPLEMENT_DYNAMIC(CHtmlCtrl, CHtmlView)
BEGIN_MESSAGE_MAP(CHtmlCtrl, CHtmlView)
	ON_WM_DESTROY()
	ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()
#pragma warning(pop)

//////////////////
// Create control in same position as an existing static control with
// the same ID (could be any kind of control, really)
//
BOOL CHtmlCtrl::CreateFromStatic(UINT nID, CWnd* pParent, CHtml_Host_Handlers *pHtml_Host_Handlers)
{
	CStatic wndStatic;
	if (!wndStatic.SubclassDlgItem(nID, pParent))
		return FALSE;

	// Get static control rect, convert to parent's client coords.
	CRect rc;
	wndStatic.GetWindowRect(&rc);
	pParent->ScreenToClient(&rc);
	wndStatic.DestroyWindow();

	m_pHtml_Host_Handlers = pHtml_Host_Handlers;
	m_pDocument2 = NULL;
	m_bCtl_mode = true;

	// create HTML control (CHtmlView)
	return Create(NULL,	NULL, (WS_CHILD | WS_VISIBLE), rc, pParent, nID, NULL);
}


BOOL CHtmlCtrl::Create(LPCTSTR lpszClassName,
							  LPCTSTR lpszWindowName,
							  DWORD dwStyle,
							  const RECT & rect,
							  CWnd * pParentWnd,
							  UINT nID,
							  CCreateContext * pContext)
{
	if (!m_pOccManager)
	{
		m_pOccManager = new CDocHostOccManager;
		ASSERT(m_pOccManager);
		if (!m_pOccManager)
			return FALSE;

		m_pOccManager->SetView( m_pHtml_Host_Handlers );
	}

	::AfxEnableControlContainer( m_pOccManager );

// The rest of the code is copied form ViewHtml.cpp MFC source file

	// create the view window itself
	m_pCreateContext = pContext;
	if (!CView::Create(lpszClassName, lpszWindowName,
				dwStyle, rect, pParentWnd,  nID, pContext))
	{
		return FALSE;
	}


	RECT rectClient;
	GetClientRect(&rectClient);

	// create the control window
	// AFX_IDW_PANE_FIRST is a safe but arbitrary ID
	if (!m_wndBrowser.CreateControl(CLSID_WebBrowser, lpszWindowName,
				WS_VISIBLE | WS_CHILD, rectClient, this, AFX_IDW_PANE_FIRST))
	{
		DestroyWindow();
		return FALSE;
	}

	
	LPUNKNOWN lpUnk = m_wndBrowser.GetControlUnknown();
	HRESULT hr = lpUnk ? lpUnk->QueryInterface(IID_IWebBrowser2, (void**) &m_pBrowserApp) : E_NOINTERFACE;
	if (!SUCCEEDED(hr))
	{
		m_pBrowserApp = NULL;
	}

	

	return TRUE;
}

#if _MFC_VER >= 0x0700

BOOL CHtmlCtrl::CreateControlSite(COleControlContainer * pContainer,
										 COleControlSite ** ppSite,
										 UINT /*nID*/,
										 REFCLSID /*clsid*/)
{
	ASSERT(m_pOccManager);
	if (!m_pOccManager)
		return FALSE;

	*ppSite = m_pOccManager->CreateSite( pContainer );

	return (*ppSite) ? TRUE : FALSE;
}

#endif

////////////////
// Override to avoid CView stuff that assumes a frame.
//
void CHtmlCtrl::OnDestroy()
{
	if (!m_bCtl_mode){ 
		CHtmlView::OnDestroy(); 
		return; 
	}

	// This is probably unecessary since ~CHtmlView does it, but
	// safer to mimic CHtmlView::OnDestroy.
	if (m_pBrowserApp) 
	{
#if _MSC_VER <= 1200
		m_pBrowserApp->Release();
#endif
		m_pBrowserApp = NULL;
	}
	CWnd::OnDestroy(); // bypass CView doc/frame stuff
}

////////////////
// Override to avoid CView stuff that assumes a frame.
//
int CHtmlCtrl::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT msg)
{
	if (!m_bCtl_mode){ 		 
		return CHtmlView::OnMouseActivate(pDesktopWnd, nHitTest, msg); 
	}

	// bypass CView doc/frame stuff
	return CWnd::OnMouseActivate(pDesktopWnd, nHitTest, msg);
}

//////////////////
// Override navigation handler to pass to "app:" links to virtual handler.
// Cancels the navigation in the browser, since app: is a pseudo-protocol.
//
void CHtmlCtrl::OnBeforeNavigate2( LPCTSTR lpszURL,
	DWORD nFlags,
	LPCTSTR lpszTargetFrameName,
	CByteArray& baPostedData,
	LPCTSTR lpszHeaders,
	BOOL* pbCancel )
{
	const char APP_PROTOCOL[] = "app:";
	size_t len = _tcslen(APP_PROTOCOL);
	if (_tcsnicmp(lpszURL, APP_PROTOCOL, len)==0) 
	{
		OnAppCmd(lpszURL + len);
		*pbCancel = TRUE;
		return;
	}

	CHtmlView::OnBeforeNavigate2(lpszURL, nFlags,
               lpszTargetFrameName, baPostedData,
               lpszHeaders, pbCancel);
}

//////////////////
// Called when the browser attempts to navigate to "app:foo"
// with "foo" as lpszWhere. Override to handle app commands.
//
void CHtmlCtrl::OnAppCmd(LPCTSTR lpszWhere)
{

	UINT cmd;
	int x;
	CString s(lpszWhere);

	cmd = (UINT)atoi(lpszWhere);
	x = s.Find("@",0);
	if ( x>=0)
		m_pHtml_Host_Handlers->_onHtmlCmd(cmd, lpszWhere+x+1);
	else
		m_pHtml_Host_Handlers->_onHtmlCmd(cmd, " ");

}


void CHtmlCtrl::OnDocumentComplete(LPCTSTR lpszURL)
{
	// this is call OnAppCmd
	HRESULT hr;
//	if(p_Body)
//		p_Body.Release();
//	if(m_pDocument2)
//		m_pDocument2.Release();
//	if(m_pDocument)
//		m_pDocument.Release();

	if (m_pDocument2==NULL)
	{
		LPDISPATCH doc = GetHtmlDocument();
		if (!doc)
			return;

		hr = doc->QueryInterface(IID_IHTMLDocument, (void**) &m_pDocument);
		if (!SUCCEEDED(hr))	{
			m_pDocument= NULL;		
			return;
		} 
		
		hr = doc->QueryInterface(IID_IHTMLDocument2, (void**) &m_pDocument2);
		if (!SUCCEEDED(hr))	{
			m_pDocument2= NULL;		
			return;
		} 
	}


	// 
	// - Get HTML Script Object.
	// - Get Name and preferred dimensions (cx, cy) of HTML
	//
	//
	//

	IDispatch *disp;
	m_pDocument->get_Script( &disp);
	SetHtmlScript(disp);	
	
	BSTR str;
	m_pDocument2->get_title(&str);	
	m_html_Title = str;
	
	IHTMLStyle *p_St;
	if(!p_Body)
		m_pDocument2->get_body(&p_Body);			
	p_Body->get_style( &p_St);

	VARIANT vt;
	vt.vt = VT_BSTR;

	hr = p_St->get_width(&vt);
	m_html_width = vt.bstrVal?_wtoi(vt.bstrVal):0;
	hr = p_St->get_height(&vt);
	m_html_height = vt.bstrVal?_wtoi(vt.bstrVal):0;

	m_pHtml_Host_Handlers->_onDocumentComplete();
	
    CHtmlView::OnDocumentComplete(lpszURL);
}

bool
CHtmlCtrl::Navigate(LPCWSTR URL)
{
	CStringW strURL(URL);
	CComBSTR bstrURL;
	bstrURL.Attach(strURL.AllocSysString());
	if (!bstrURL.Length())
		return false;

	COleSafeArray vPostData;

	HRESULT res = E_UNEXPECTED;
	if (m_pBrowserApp)
		res = m_pBrowserApp->Navigate(bstrURL,
			COleVariant((long) navNoReadFromCache | navNoWriteToCache | navNoHistory, VT_I4),
			COleVariant((LPCSTR)NULL, VT_BSTR),
			vPostData,
			COleVariant((LPCSTR)NULL, VT_BSTR));
	return SUCCEEDED(res);
}
