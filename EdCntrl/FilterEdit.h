#if !defined(FilterEdit_h__)
#define FilterEdit_h__

template <class BASE = CEdit> class FilterEdit : public BASE
{
  public:
	FilterEdit() : mBuddyList(NULL)
	{
	}

	void SetListBuddy(HWND hList)
	{
		mBuddyList = hList;
	}
	void WantReturn()
	{
		mWantReturn = true;
	}

  protected:
	// for processing Windows messages
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (mBuddyList)
		{
			if (WM_KEYDOWN == message)
			{
				switch (wParam)
				{
				case VK_UP:
				case VK_DOWN:
				case VK_NEXT:
				case VK_PRIOR:
					return ::SendMessage(mBuddyList, message, wParam, lParam);
				case VK_RETURN:
					if (mWantReturn)
						return ::SendMessage(mBuddyList, message, wParam, lParam);
					break;
				case VK_LEFT:
				case VK_RIGHT: {
					CString curText;
					BASE::GetWindowText(curText);
					if (curText.IsEmpty())
					{
						// pass left and right to buddy if no text entered yet
						return ::SendMessage(mBuddyList, message, wParam, lParam);
					}
				}
				break;
				case VK_HOME:
				case VK_END:
				case VK_SHIFT: {
					CString curText;
					BASE::GetWindowText(curText);
					if (curText.IsEmpty())
					{
						// [case: 37858] pass home and end to buddy if no text entered yet
						return ::SendMessage(mBuddyList, message, wParam, lParam);
					}
					else if (GetKeyState(VK_CONTROL) & 0x1000)
					{
						// [case: 80606] pass ctrl+home and ctrl+end to buddy
						return ::SendMessage(mBuddyList, message, wParam, lParam);
					}
				}
				break;
				}
			}
			else if (WM_MOUSEWHEEL == message)
				return ::SendMessage(mBuddyList, message, wParam, lParam);
		}
		return BASE::WindowProc(message, wParam, lParam);
	}

  private:
	HWND mBuddyList;
	bool mWantReturn = false;
};

#endif // FilterEdit_h__
