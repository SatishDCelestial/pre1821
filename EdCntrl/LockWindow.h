#pragma once

class CLockWindow
{
  public:
	CLockWindow(HWND hwnd)
	{
		assert(hwnd);
		::LockWindowUpdate(hwnd);
		locked.push_back(hwnd);
	}

	~CLockWindow()
	{
		assert(locked.size() > 0);
		HWND hwnd = locked.back();
		locked.pop_back();

		if (locked.size())
		{
			if (locked.back() == hwnd)
				return;
			::LockWindowUpdate(locked.back());
		}
		else
			::LockWindowUpdate(NULL);
	}

  protected:
	static std::vector<HWND> locked;
};

#ifdef LOCK_WINDOW_IMPLEMENT
std::vector<HWND> CLockWindow::locked;
#endif
