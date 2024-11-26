#pragma once

#include "resource.h"
#include "VADialog.h"
#include "CtrlBackspaceEdit.h"
#include "VAThemeDraw.h"

// ExtractMethodDlg dialog

class ExtractMethodDlg : public CThemedVADlg
{
	DECLARE_DYNAMIC(ExtractMethodDlg)

  public:
	ExtractMethodDlg(CWnd* pParent, const WTString& defText, const WTString& impText, const WTString& impText_free,
	                 BOOL canExtractToSrource = NULL, BOOL canExtractAsFreeFunction = FALSE,
	                 const WTString& customTitle = WTString()); // standard constructor
	virtual ~ExtractMethodDlg();

	// Dialog Data
	enum
	{
		IDD = IDD_EXTRACTMETHOD
	};
	virtual BOOL OnInitDialog();
	WTString m_newName;
	WTString m_orgName;
	WTString m_orgSignature;
	WTString m_newSignature;
	WTString m_orgSignature_Free;
	BOOL m_canExtractToSrc;
	CtrlBackspaceEdit<CThemedEditSHL<>> m_newName_ctrl;
	CtrlBackspaceEdit<CThemedEditACID<>> m_newSignature_ctrl;
	BOOL m_extractAsFreeFunction;
	BOOL m_canExtractAsFreeFunction;

	bool IsExtractToSrc();

  protected:
	WTString m_customTitle;

	BOOL m_extractToSrc;

	virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV support

	void UpdateSignature();
	void EnableDisableExtractToSource();
	void LogForTest();

	DECLARE_MESSAGE_MAP()

	afx_msg void OnBnClickedOk();
	afx_msg void OnNameChanged();
	afx_msg void OnCheckFreeFunction();
};
