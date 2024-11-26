// SpellCheckDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "SpellCheckDlg.h"
#include "myspell\WTHashList.h"
#include "WindowUtils.h"
#include "VAAutomation.h"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CSpellCheckDlg dialog

WTString SpellWordDlg(const WTString& text)
{
	WTString res;
	CSpellCheckDlg spellDlg(CWnd::GetFocus(), text);
	const int dlgRes = (int)spellDlg.DoModal();
	if (IDOK == dlgRes)
	{
		static WTString txt;
		txt = spellDlg.m_changeTo;
		res = txt;
	}

	if (gTestLogger)
	{
		WTString msg;
		msg.WTFormat("SpellChkDlg: dlgRes(%d) inText(%s) outText(%s)", dlgRes, text.c_str(), res.c_str());
		gTestLogger->LogStr(msg);
	}

	return res;
}

CSpellCheckDlg::CSpellCheckDlg(CWnd* pParent /*=NULL*/, const WTString& text)
    : CThemedVADlg(CSpellCheckDlg::IDD, pParent), mOriginalWord(text)
{
	//{{AFX_DATA_INIT(CSpellCheckDlg)
	//}}AFX_DATA_INIT
}

void CSpellCheckDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CSpellCheckDlg)
	DDX_Control(pDX, IDC_LIST1, m_suggListCtl);
	DDX_Control(pDX, IDC_CHANGE, m_changeBtn);
	//}}AFX_DATA_MAP

	if (!pDX->m_bSaveAndValidate)
	{
		m_suggListCtl.ResetContent();
		if (FPSSpell(mOriginalWord.c_str(), &m_suggestList) && m_suggestList.GetCount())
		{
			for (int i = 0; i < m_suggestList.GetCount(); i++)
				m_suggListCtl.InsertString(i, m_suggestList.GetAt(m_suggestList.FindIndex(i)));
		}
		else
		{
			m_suggListCtl.InsertString(0, mOriginalWord.c_str());
		}
		SetDlgItemTextW(m_hWnd, IDC_EDIT1, mOriginalWord.Wide());
		m_changeTo = mOriginalWord;
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(CSpellCheckDlg, CThemedVADlg)
//{{AFX_MSG_MAP(CSpellCheckDlg)
ON_BN_CLICKED(IDC_ADD, OnAdd)
ON_BN_CLICKED(IDC_CHANGE, OnChange)
ON_BN_CLICKED(IDC_IGNOREALL, OnIgnoreAll)
ON_BN_CLICKED(IDC_OPTIONS, OnOptions)
ON_LBN_SELCHANGE(IDC_LIST1, OnSuggestionListSelChange)
ON_EN_CHANGE(IDC_EDIT1, OnChangeEdit1)
ON_LBN_DBLCLK(IDC_LIST1, OnDblclkSuggestionList)
ON_BN_CLICKED(IDC_IGNORE, OnIgnore)
ON_WM_SHOWWINDOW()
ON_WM_DESTROY()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// CSpellCheckDlg message handlers
extern void FPSAddWord(LPCSTR text, BOOL ignore);

BOOL CSpellCheckDlg::OnInitDialog()
{
	__super::OnInitDialog();

	mEdit.SubclassDlgItem(IDC_EDIT1, this);

	AddSzControl(IDC_EDIT1, mdResize, mdNone);
	AddSzControl(IDC_LIST1, mdResize, mdResize);
	AddSzControl(IDC_CHANGE, mdRepos, mdNone);
	AddSzControl(IDC_IGNORE, mdRepos, mdNone);
	AddSzControl(IDC_IGNOREALL, mdRepos, mdNone);
	AddSzControl(IDC_ADD, mdRepos, mdNone);
	AddSzControl(IDCANCEL, mdRepos, mdNone);
	AddSzControl(IDC_OPTIONS, mdRepos, mdNone);

	::mySetProp(m_suggListCtl.m_hWnd, "__VA_do_not_colour", (HANDLE)1);
	CWnd* pEdit = GetDlgItem(IDC_EDIT1);
	::mySetProp(pEdit->GetSafeHwnd(), "__VA_do_not_colour", (HANDLE)1);

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_IGNORE, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_IGNOREALL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_ADD, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_OPTIONS, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	VAUpdateWindowTitle(VAWindowType::SpellCheck, *this);

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void CSpellCheckDlg::UseOriginalWord()
{
	OnOK();
	m_changeTo = mOriginalWord;
}

void CSpellCheckDlg::OnAdd()
{
	UseOriginalWord();
	FPSAddWord(m_changeTo.c_str(), FALSE);
}

void CSpellCheckDlg::OnIgnoreAll()
{
	OnIgnore();
	FPSAddWord(m_changeTo.c_str(), TRUE);
}

void CSpellCheckDlg::OnChange()
{
	// m_changeTo will have already been set by either
	// OnDblclkSuggestionList, OnChangeEdit1 or OnSuggestionListSelChange
	ASSERT(m_changeTo.GetLength());
	OnOK();
}

void CSpellCheckDlg::OnDblclkSuggestionList()
{
	if (m_suggListCtl.GetCurSel() >= 0)
	{
		LPCTSTR str = (LPCTSTR)m_suggListCtl.GetItemData(m_suggListCtl.GetCurSel());
		if (str && *str)
			m_changeTo = str;
		else
			m_changeTo.Empty();
	}

	if (!m_changeTo.IsEmpty())
		OnChange();
}

void CSpellCheckDlg::OnIgnore()
{
	UseOriginalWord();
}

void CSpellCheckDlg::OnOptions()
{
}

void CSpellCheckDlg::OnSuggestionListSelChange()
{
	if (m_suggListCtl.GetCurSel() >= 0)
	{
		LPCTSTR str = (LPCTSTR)m_suggListCtl.GetItemData(m_suggListCtl.GetCurSel());
		if (str && *str)
			m_changeTo = str;
		else
			m_changeTo.Empty();
	}
	else
		m_changeTo.Empty();

	m_changeBtn.EnableWindow(m_changeTo != mOriginalWord && m_changeTo.GetLength());
}

void CSpellCheckDlg::OnChangeEdit1()
{
	// TODO: If this is a RICHEDIT control, the control will not
	// send this notification unless you override the VADialog::OnInitDialog()
	// function and call CRichEditCtrl().SetEventMask()
	// with the ENM_CHANGE flag ORed into the mask.

	CStringW changeToTemp;
	mEdit.GetText(changeToTemp);
	m_changeTo = changeToTemp;

	m_suggListCtl.ResetContent();
	FPSSpell(m_changeTo.c_str(), &m_suggestList);
	if (!m_suggestList.GetCount())
	{
		m_suggListCtl.InsertString(0, m_changeTo.c_str());
	}
	else
	{
		for (int i = 0; i < m_suggestList.GetCount(); i++)
			m_suggListCtl.InsertString(i, m_suggestList.GetAt(m_suggestList.FindIndex(i)));
	}
	m_changeBtn.EnableWindow(m_changeTo != mOriginalWord && m_changeTo.GetLength());
}

static CRect s_SpellRect(0, 0, 0, 0);

void CSpellCheckDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	if (s_SpellRect.Width())
		MoveWindow(&s_SpellRect);
	__super::OnShowWindow(bShow, nStatus);
}

void CSpellCheckDlg::OnDestroy()
{
	GetWindowRect(&s_SpellRect);
	__super::OnDestroy();
}
