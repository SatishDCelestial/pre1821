#pragma once

#include "VADialog.h"
#include "resource.h"

// VAProgress dialog

class VAProgress : public CDialog
{
	DECLARE_DYNAMIC(VAProgress)
	CProgressCtrl* mProgress = nullptr;
	int mVal = 0;

  public:
	BOOL mCanceled = FALSE;
	void DisplayProgress(int percentage);
	void SetProgress(int percentage);
	void IncProgress();
	VAProgress(); // standard constructor

	// Dialog Data
	enum
	{
		IDD = IDD_CREATEMETHODDLG
	};

  protected:
	DECLARE_MESSAGE_MAP()
  public:
	virtual void OnCancel();

  private:
};
