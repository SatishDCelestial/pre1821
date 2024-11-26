// VAProgress.cpp : implementation file
//

#include "stdafxed.h"
#include "VAProgress.h"
#include "Colourizer.h"
#include "VAAutomation.h"
#include "PROJECT.H"
#include "resource.h"

// VAProgress dialog

IMPLEMENT_DYNAMIC(VAProgress, CDialog)
VAProgress::VAProgress() : CDialog(VAProgress::IDD, NULL)
{
}

// void VAProgress::DoDataExchange(CDataExchange* pDX)
// {
// 	__super::DoDataExchange(pDX);
// 	DDX_Text(pDX, IDC_EDIT1, mText);
// }

BEGIN_MESSAGE_MAP(VAProgress, CDialog)
END_MESSAGE_MAP()

// VAProgress message handlers

void VAProgress::OnCancel()
{
	mCanceled = TRUE;
	__super::OnCancel();
}

void VAProgress::SetProgress(int percentage)
{
	if (mProgress)
		mProgress->SetPos((short)percentage);
}

void VAProgress::DisplayProgress(int count)
{
	mVal = 0;
	if (!mProgress && Create(IDD_VAPROGRESS, NULL))
	{
		CRect r, rc;
		gMainWnd->GetClientRect(&r);
		GetWindowRect(rc);
		SetParent(gMainWnd);
		this->ModifyStyle(WS_POPUP, WS_CHILD);
		MoveWindow(r.left, r.bottom - 40, 400, 40);
		mProgress = (CProgressCtrl*)GetDlgItem(IDC_PROGRESS1);
		ShowWindow(SW_NORMAL);
		EnableWindow(TRUE);
		RedrawWindow();
	}
	if (mProgress)
		mProgress->SetRange(0, (short)count);
}

void VAProgress::IncProgress()
{
	mVal++;
	if (mProgress)
		mProgress->SetPos(mVal);
}
