#pragma once
#include "afxwin.h"
#include <afxdialogex.h>
#include "resource.h"


// MaintenanceRenewal_MultiUser dialog

class MaintenanceRenewal_MultiUser : public CDialog
{
	DECLARE_DYNAMIC(MaintenanceRenewal_MultiUser)

public:
	MaintenanceRenewal_MultiUser(CWnd* pParent = NULL);   // standard constructor
	virtual ~MaintenanceRenewal_MultiUser();

	// Dialog Data
	enum { IDD = IDD_MAINTENANCERENEWAL_MULTIUSER };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	CString mMessageStr;
	CButton mRemindLater;
	CStatic mMessage;
	bool m_bRemaindLater;

	afx_msg void OnBnClickedCheckReminder();
	afx_msg void OnBnClickedButtonEmail();
	afx_msg void OnBnClickedButtonGetQuote();
};
