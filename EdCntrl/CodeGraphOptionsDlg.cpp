// CodeGraphOptionsDlg.cpp : implementation file
//

#include "stdafxed.h"
#include "CodeGraphOptionsDlg.h"
#include "afxdialogex.h"
#include "Registry.h"
#include "RegKeys.h"

// CodeGraphOptionsDlg dialog

IMPLEMENT_DYNAMIC(CodeGraphOptionsDlg, CDialog)

CodeGraphOptionsDlg::CodeGraphOptionsDlg(CWnd* pParent /*=NULL*/) : CDialog(CodeGraphOptionsDlg::IDD, pParent)
{
	m_bLinkAllReferences = !!GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "GraphLinkAllReferences", TRUE);
	m_bLinkMethods = !m_bLinkAllReferences;
	m_bGroupByProject = !!GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "GraphGroupByProject", TRUE);
	m_bGroupByClass = !m_bGroupByProject;
}

CodeGraphOptionsDlg::~CodeGraphOptionsDlg()
{
}

void CodeGraphOptionsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_RADIO_LINK_BY_CALLS, m_bLinkMethods);
	DDX_Check(pDX, IDC_RADIO_LINK_BY_ALL_REFERENCES, m_bLinkAllReferences);
	DDX_Check(pDX, IDC_RADIO_GROUP_BY_NAMESPACE, m_bGroupByProject);
	DDX_Check(pDX, IDC_RADIO_GROUP_BY_CLASS, m_bGroupByClass);
}

BEGIN_MESSAGE_MAP(CodeGraphOptionsDlg, CDialog)
END_MESSAGE_MAP()

void CodeGraphOptionsDlg::OnOK()
{
	__super::OnOK();
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "GraphShowReferenceLinks", m_bLinkMethods ? 1u : 0u);
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "GraphGroupByProject", m_bGroupByProject ? 1u : 0u);
}

// CodeGraphOptionsDlg message handlers
