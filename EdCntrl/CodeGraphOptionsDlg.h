#pragma once
#include "resource.h"
#include "afxwin.h"

// CodeGraphOptionsDlg dialog

class CodeGraphOptionsDlg : public CDialog
{
	DECLARE_DYNAMIC(CodeGraphOptionsDlg)

  public:
	CodeGraphOptionsDlg(CWnd* pParent = NULL); // standard constructor
	virtual ~CodeGraphOptionsDlg();

	// Dialog Data
	enum
	{
		IDD = IDD_GRAPH_OPTIONS
	};

  protected:
	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
  public:
	BOOL m_bLinkMethods;
	BOOL m_bLinkAllReferences;
	BOOL m_bGroupByProject;
	BOOL m_bGroupByClass;
};
