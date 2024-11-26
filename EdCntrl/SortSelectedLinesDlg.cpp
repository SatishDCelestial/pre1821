// SortSelectedLinesDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "SortSelectedLinesDlg.h"
#include "afxdialogex.h"
#include "resource.h"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// SortSelectedLinesDlg dialog

IMPLEMENT_DYNAMIC(SortSelectedLinesDlg, CDialog)

SortSelectedLinesDlg::SortSelectedLinesDlg(CWnd* pParent /*=NULL*/)
    : CThemedVADlg(IDD_SORTSELECTEDLINES, pParent, cdxCDynamicWnd::fdNone, flAntiFlicker | flNoSavePos)
{
}

SortSelectedLinesDlg::~SortSelectedLinesDlg()
{
}

void SortSelectedLinesDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_SSL_ASCENDING, ComboAscending);
	DDX_Control(pDX, IDC_SSL_DESCENDING, ComboDescending);
	DDX_Control(pDX, IDC_SSL_SENSITIVE, ComboSensitive);
	DDX_Control(pDX, IDC_SSL_INSENSITIVE, ComboInsensitive);
}

BOOL SortSelectedLinesDlg::OnInitDialog()
{
	const BOOL retval = __super::OnInitDialog();

	ComboAscending.SetCheck(TRUE);
	AscDesc = eAscDesc::ASCENDING;
	ComboSensitive.SetCheck(TRUE);
	Case = eCase::SENSITIVE;

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	VAUpdateWindowTitle(VAWindowType::SortSelectedLines, *this);

	SetHelpTopic("dlgSortSelectedLines");

	return retval;
}

void SortSelectedLinesDlg::OnBnClickedAscending()
{
	AscDesc = eAscDesc::ASCENDING;
}

void SortSelectedLinesDlg::OnBnClickedDescending()
{
	AscDesc = eAscDesc::DESCENDING;
}

void SortSelectedLinesDlg::OnBnClickedSensitive()
{
	Case = eCase::SENSITIVE;
}

void SortSelectedLinesDlg::OnBnClickedInsensitive()
{
	Case = eCase::INSENSITIVE;
}

BEGIN_MESSAGE_MAP(SortSelectedLinesDlg, CThemedVADlg)
ON_BN_CLICKED(IDC_SSL_ASCENDING, &SortSelectedLinesDlg::OnBnClickedAscending)
ON_BN_CLICKED(IDC_SSL_DESCENDING, &SortSelectedLinesDlg::OnBnClickedDescending)
ON_BN_CLICKED(IDC_SSL_SENSITIVE, &SortSelectedLinesDlg::OnBnClickedSensitive)
ON_BN_CLICKED(IDC_SSL_INSENSITIVE, &SortSelectedLinesDlg::OnBnClickedInsensitive)
END_MESSAGE_MAP()

// SortSelectedLinesDlg message handlers
