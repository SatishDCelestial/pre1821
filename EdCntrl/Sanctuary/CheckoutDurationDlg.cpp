#include "StdAfxEd.h"

#if defined(SANCTUARY)

#include "CheckoutDurationDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IMPLEMENT_DYNAMIC(CheckoutDurationDlg, VADialog)

CheckoutDurationDlg::CheckoutDurationDlg(CWnd* pParent /*=NULL*/)
    : CThemedVADlg(IDD_CheckoutDurationDlg, pParent, cdxCDynamicWnd::fdNone, flAntiFlicker | flNoSavePos)
{
}

void CheckoutDurationDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	if (pDX->m_bSaveAndValidate)
	{
		CStringW tmp;
		mEdit.GetText(tmp);
		mDuration = _wtoi(tmp);
	}
}

BOOL CheckoutDurationDlg::OnInitDialog()
{
	const BOOL retval = __super::OnInitDialog();

	if (GetDlgItem(IDC_EDIT1))
	{
		// [case: 9194] do not use DDX_Control due to ColourizeControl.
		// Subclass with colourizer before SHAutoComplete (CtrlBackspaceEdit).
		mEdit.SubclassWindow(GetDlgItem(IDC_EDIT1)->m_hWnd);

		CStringW txt;
		CString__FormatW(txt, L"%d", mDuration);
		mEdit.SetText(txt);
	}

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming((UINT)IDC_STATIC);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	//	SetHelpTopic("dlgCheckoutDuration");

	return retval;
}

BEGIN_MESSAGE_MAP(CheckoutDurationDlg, CThemedVADlg)
END_MESSAGE_MAP()

#endif
