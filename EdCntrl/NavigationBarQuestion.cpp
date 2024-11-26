#include "stdafxed.h"
#include "NavigationBarQuestion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// NavigationBarQuestion dialog

IMPLEMENT_DYNAMIC(NavigationBarQuestion, VADialog)

NavigationBarQuestion::NavigationBarQuestion(CWnd* pParent /*=NULL*/)
    : CThemedVADlg(NavigationBarQuestion::IDD, pParent, cdxCDynamicWnd::fdNone, flAntiFlicker | flNoSavePos)
{
	SetHelpTopic("top");
}

NavigationBarQuestion::~NavigationBarQuestion()
{
}

void NavigationBarQuestion::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_CHECK_ASKMELATER, mAskMeLater);
	DDX_Control(pDX, IDC_NAVBAR_MESSAGE, mMessage);
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(NavigationBarQuestion, CThemedVADlg)
ON_BN_CLICKED(IDC_CHECK_ASKMELATER, &NavigationBarQuestion::OnBnClickedAskMeLater)
ON_WM_SIZE()
END_MESSAGE_MAP()
#pragma warning(pop)

BOOL NavigationBarQuestion::OnInitDialog()
{
	const BOOL retval = __super::OnInitDialog();

	mAskMeLater.SetCheck(TRUE);
	m_bAskMeLater = true;

	mMessage.SetWindowText("Visual Assist has detected that both the Visual Assist and IDE navigation bars are "
	                       "visible. Because the Visual Assist navigation bar includes substantially all of the "
	                       "functionality of the IDE version, would you like to hide the IDE version to reduce clutter "
	                       "and reclaim space? You can change your mind in the Visual Assist and IDE options dialogs.");

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDOK, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	return retval;
}

void NavigationBarQuestion::OnBnClickedAskMeLater()
{
	m_bAskMeLater = !!mAskMeLater.GetCheck();
}
