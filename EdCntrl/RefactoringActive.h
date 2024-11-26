#pragma once

class RefactoringActive
{
	static volatile LONG sIsActive;
	static volatile int sCurrentRefactoring;

  public:
	RefactoringActive()
	{
		::InterlockedIncrement(&sIsActive);
	}
	~RefactoringActive()
	{
		::InterlockedDecrement(&sIsActive);
	}

	static bool IsActive()
	{
		return sIsActive > 0;
	}

	// This is a hack for the RenameFilesDlg to work around IsActive()
	// this won't work when multiple callers try to set this
	static void SetCurrentRefactoring(int r)
	{
		_ASSERTE(IsActive());
		sCurrentRefactoring = r;
	}
	static int GetCurrentRefactoring()
	{
		_ASSERTE(IsActive());
		return sCurrentRefactoring;
	}
};
