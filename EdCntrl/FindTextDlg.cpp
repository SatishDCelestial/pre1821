#include "stdafxed.h"
#include "FindTextDlg.h"
#include "RedirectRegistryToVA.h"
#include "StringUtils.h"
#include "IFindTargetWithTree.h"
#include "mctree/ColumnTreeWnd.h"
#include "WindowUtils.h"
#include "vaIPC\vaIPC\common\string_utils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IMPLEMENT_DYNAMIC(FindTextDlg, VADialog)

BEGIN_MESSAGE_MAP(FindTextDlg, CThemedVADlg)
ON_WM_CLOSE()
ON_BN_CLICKED(IDC_FIND_NEXT, &FindTextDlg::OnBnClickedFindNext)
ON_BN_CLICKED(IDC_FIND_PREV, &FindTextDlg::OnBnClickedFindPrev)
ON_BN_CLICKED(IDC_GOTO, &FindTextDlg::OnBnClickedGoto)
ON_CBN_EDITCHANGE(IDC_FINDEDIT, &FindTextDlg::OnCbnEditchangeFindedit)
ON_CBN_SELCHANGE(IDC_FINDEDIT, &FindTextDlg::OnCbnSelchangeFindedit)
ON_BN_CLICKED(IDC_MARKALL, &FindTextDlg::OnBnClickedMarkall)
ON_BN_CLICKED(IDC_MATCH_CASE, &FindTextDlg::OnBnClickedMatchCase)
END_MESSAGE_MAP()

const int max_find_history = 15;

FindTextDlg::FindTextDlg(IFindTargetWithTree<CColumnTreeCtrl>* theFindTarget)
    : CThemedVADlg(FindTextDlg::IDD, theFindTarget->GetCWnd(), fdHoriz, flSizeIcon | flAntiFlicker),
      mSearchTarget(*theFindTarget)
{
}

FindTextDlg::~FindTextDlg()
{
}

void FindTextDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	// DDX_Control(pDX, IDC_FINDEDIT, m_find);
	DDX_Control(pDX, IDC_MARKALL, m_markall);
	DDX_Control(pDX, IDC_FIND_NEXT, m_find_next);
	DDX_Control(pDX, IDC_FIND_PREV, m_find_prev);
	DDX_Control(pDX, IDC_GOTO, m_goto);
	DDX_Control(pDX, IDC_MATCH_CASE, m_matchcase);
}

BOOL FindTextDlg::OnInitDialog()
{
	__super::OnInitDialog();

	AddSzControl(IDC_FINDEDIT, mdResize, mdNone);
	AddSzControl(IDC_FIND_NEXT, mdRepos, mdNone);
	AddSzControl(IDC_FIND_PREV, mdRepos, mdNone);
	AddSzControl(IDC_MARKALL, mdRepos, mdNone);
	AddSzControl(IDCANCEL, mdRepos, mdNone);
	//	AddSzControl(IDC_FINDUP, mdRelative, mdNone);
	AddSzControl(IDC_GOTO, mdRepos, mdNone);

	mySetProp(::GetDlgItem(m_hWnd, IDC_FINDEDIT), "__VA_do_not_colour", (HANDLE)1);

	m_find.SetPopupDropShadow(true);
	m_find.SubclassDlgItemAndInit(m_hWnd, IDC_FINDEDIT);
	m_find.AddCtrlBackspaceEditHandler();

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming(0xFFFFFFFF);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_FINDDOWN, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedRadioButton>(IDC_FINDUP, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	// 	if(cbi.hwndItem && ::IsWindow(cbi.hwndItem))
	// 		m_find_edit.SubclassWindowW(cbi.hwndItem);

	CRedirectRegistryToVA rreg;

	m_find.SetText(AfxGetApp()->GetProfileString("Find", "find", ""));

	m_markall.SetCheck(_stricmp(AfxGetApp()->GetProfileString("Find", "markall", "no"), "yes") ? 0 : 1);
	m_matchcase.SetCheck(_stricmp(AfxGetApp()->GetProfileString("Find", "match_case", "no"), "yes") ? 0 : 1);
	CheckRadioButton(IDC_FINDUP, IDC_FINDDOWN,
	                 _stricmp(AfxGetApp()->GetProfileString("Find", "direction", "down"), "up") ? IDC_FINDDOWN
	                                                                                            : IDC_FINDUP);

	for (int i = 0; i < max_find_history; i++)
	{
		CString history = AfxGetApp()->GetProfileString("Find", string_format("history_%d", i + 1).c_str(), "");
		if (!history.GetLength())
			break;
		m_find.AddItemA(history);
	}

	if (mSearchTarget.GetTree().GetLastFound() != mSearchTarget.GetTree().GetTreeCtrl().GetSelectedItem())
		mSearchTarget.UnmarkAll(unmark_all);
	UpdateMarkAll();

	m_find.SetFocus();
	return true;
}

void FindTextDlg::OnClose()
{
	__super::OnClose();
}

BOOL FindTextDlg::DestroyWindow()
{
	CRedirectRegistryToVA rreg;

	// if updated, synchronize with FindUsageDlg.cpp
	CStringA find; // safe use of CStringA
	m_find.GetText(find);
	AfxGetApp()->WriteProfileString("Find", "find", find);

	AfxGetApp()->WriteProfileString("Find", "markall", m_markall.GetCheck() ? "yes" : "no");
	AfxGetApp()->WriteProfileString("Find", "match_case", m_matchcase.GetCheck() ? "yes" : "no");

	AfxGetApp()->WriteProfileString("Find", "direction",
	                                (GetCheckedRadioButton(IDC_FINDUP, IDC_FINDDOWN) == IDC_FINDUP) ? "up" : "down");

	for (int i = 0; i < min(max_find_history, m_find.GetItemsCount()); i++)
	{
		CStringA text; // safe use of CStringA
		m_find.Items->At((uint)i).GetText(text);
		AfxGetApp()->WriteProfileString("Find", string_format("history_%d", i + 1).c_str(), text);
	}
	for (int i = m_find.GetItemsCount(); i < max_find_history; i++)
		AfxGetApp()->WriteProfileString("Find", string_format("history_%d", i + 1).c_str(), "");

	return __super::DestroyWindow();
}

void FindTextDlg::OnBnClickedFindNext()
{
	HandleFind(true);
}

void FindTextDlg::OnBnClickedFindPrev()
{
	HandleFind(false);
}

void FindTextDlg::HandleFind(bool next)
{
	CStringW find;
	m_find.GetText(find);

	if (!find.GetLength())
		return;

	int i = m_find.FindItemW(find);
	if (i >= 0)
	{
		m_find.RemoveItemAt(i);
	}

	m_find.InsertItemW(0, find);

	if (m_find.GetCount() > max_find_history)
		m_find.RemoveItemAt(max_find_history);

	mSearchTarget.SetFindText(find);
	mSearchTarget.SetFindCaseSensitive(!!m_matchcase.GetCheck());
	mSearchTarget.SetFindReverse(GetCheckedRadioButton(IDC_FINDUP, IDC_FINDDOWN) == IDC_FINDUP);
	mSearchTarget.SetMarkAll(!!m_markall.GetCheck());

	if (next)
	{
		mSearchTarget.OnFindNext();
	}
	else
	{
		mSearchTarget.OnFindPrev();
	}

	UpdateGUI();
}

void FindTextDlg::OnCbnEditchangeFindedit()
{
	UpdateGUI();
	UpdateMarkAll();
}

void FindTextDlg::OnCbnSelchangeFindedit()
{
	UpdateGUI();
	UpdateMarkAll();
}

void FindTextDlg::UpdateGUI()
{
	CStringW find;
	m_find.GetText(find);

	m_find_next.EnableWindow(find.GetLength() > 0);
	m_find_prev.EnableWindow(find.GetLength() > 0);

	//	m_goto.EnableWindow(!!mSearchTarget.GetTree().GetLastFound());
	m_goto.EnableWindow(!!mSearchTarget.GetTree().GetTreeCtrl().GetSelectedItem());

	m_find.SetText(find);
}

void FindTextDlg::OnBnClickedGoto()
{
	//	HTREEITEM lastfound = mSearchTarget.GetTree().GetLastFound();
	HTREEITEM lastfound = mSearchTarget.GetTree().GetTreeCtrl().GetSelectedItem();
	if (lastfound)
	{
		PostMessage(WM_CLOSE);

		mSearchTarget.GetTree().GetTreeCtrl().SelectItem(lastfound);
		mSearchTarget.GoToSelectedItem();
	}
}

BOOL FindTextDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->wParam == VK_ESCAPE)
	{ // escape key intercepted to prevent closing findref window in VC6
		if (pMsg->message == WM_KEYDOWN)
		{
			return false;
		}
		else if (pMsg->message == WM_KEYUP)
		{
			PostMessage(WM_CLOSE);
			return false;
		}
	}

	return __super::PreTranslateMessage(pMsg);
}

void FindTextDlg::OnBnClickedMarkall()
{
	if (m_markall.GetCheck())
	{
		UpdateMarkAll();
	}
	else
	{
		mSearchTarget.UnmarkAll();
	}
}

void FindTextDlg::OnBnClickedMatchCase()
{
	UpdateMarkAll();
}

void FindTextDlg::UpdateMarkAll()
{
	if (!m_markall.GetCheck())
		return;

	mSearchTarget.UnmarkAll();
	CStringW findTxt;
	m_find.GetText(findTxt);

	mSearchTarget.SetFindText(findTxt);
	mSearchTarget.SetFindCaseSensitive(!!m_matchcase.GetCheck());
	mSearchTarget.SetMarkAll(!!m_markall.GetCheck());
	mSearchTarget.MarkAll();
}
