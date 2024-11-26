#pragma once

class PauseRedraw
{
	CWnd* mWnd;
	bool mDoRedraw;

  public:
	PauseRedraw(CWnd* wnd, bool doRedraw) : mWnd(wnd), mDoRedraw(doRedraw)
	{
		_ASSERTE(mWnd);
		mWnd->SetRedraw(FALSE);
	}

	~PauseRedraw()
	{
		mWnd->SetRedraw(TRUE);
		if (mDoRedraw)
			mWnd->RedrawWindow();
	}
};
