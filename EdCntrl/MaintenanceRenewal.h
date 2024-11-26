#pragma once
#include <afxdialogex.h>
#include "resource.h"
#include "afxwin.h"


// MaintenanceRenewal dialog

class MaintenanceRenewal : public CDialog
{
	DECLARE_DYNAMIC(MaintenanceRenewal)

public:
	MaintenanceRenewal(CWnd* pParent = NULL);   // standard constructor
	virtual ~MaintenanceRenewal();

// Dialog Data
	enum { IDD = IDD_MAINTENANCERENEWAL };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()

	virtual BOOL OnInitDialog();

public:
	CStatic mMessage;
	CString mMessageStr;
	CButton mRemindLater;
	bool m_bRemaindLater;

	afx_msg void OnBnClickedCheckRemindLater();
};
