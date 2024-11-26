#pragma once

#include "DevShellAttributes.h"
#include "DevShellService.h"
#include "EDCNT.H"
#include "WtException.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "SubClassWnd.h"
#include "StringUtils.h"
#include "UnicodeHelper.h"

void WaitThreads(BOOL waitForRefactoring = TRUE);

class KeyboardLayoutToggle
{
	typedef std::vector<HKL> HKLVector;

	HKL m_old_hkl;
	HKL m_new_hkl;
	bool m_unload;

	static void DoEvents()
	{
		Sleep(1);
	}

	static bool GetLoadedLayouts(HKLVector& list)
	{
		size_t size = (size_t)::GetKeyboardLayoutList(0, nullptr);
		list.resize(size, 0);

		if (size)
			return 0 < ::GetKeyboardLayoutList((int)size, &list.front());

		return false;
	}

	static bool LayoutIsLoaded(HKL hkl)
	{
		HKLVector list;

		if (GetLoadedLayouts(list))
			for (HKL h : list)
				if (h == hkl)
					return true;

		return false;
	}

	static HKL ActivateLayout(HKL hkl)
	{
		HKL old_layout = ::GetKeyboardLayout(0);

		for (int tries = 0; tries < 5; tries++)
		{
			ActivateKeyboardLayout(hkl, 0);

			DoEvents();

			if (::GetKeyboardLayout(0) != old_layout)
				break;
		}

		if (::GetKeyboardLayout(0) == old_layout)
			return 0;

		return old_layout;
	}

	static HKL LoadOrActivateLayout(HKL hkl)
	{
		if (LayoutIsLoaded(hkl))
		{
			ActivateLayout(hkl);
		}
		else
		{
			HKL old_layout = ::GetKeyboardLayout(0);

			// print new layout name
			CString buff;
			CString__FormatA(buff, _T("%p"), hkl);

			for (int tries = 0; tries < 5; tries++)
			{
				// load layout by name
				::LoadKeyboardLayout(buff, KLF_ACTIVATE);

				DoEvents();

				if (::GetKeyboardLayout(0) != old_layout)
					break;
			}

			if (::GetKeyboardLayout(0) == old_layout)
				return 0;
		}

		return ::GetKeyboardLayout(0);
	}

  public:
	KeyboardLayoutToggle(HKL hkl);
	~KeyboardLayoutToggle();
};

class KeyPressSimulator
{
	WCHAR m_key;
	BOOL m_virtualKey;

  public:
	KeyPressSimulator(WCHAR key, BOOL isVirtualKey = FALSE)
	{
		m_key = key;
		m_virtualKey = isVirtualKey;
		SendKey((SHORT)m_key, 0);
	}
	~KeyPressSimulator()
	{
		SendKey((SHORT)m_key, KEYEVENTF_KEYUP);
	}
	void SendKey(SHORT vk, DWORD flags);
};

static void SimulateKeyPress(WCHAR key, BOOL isVirtualKey = FALSE)
{
	KeyPressSimulator kps(key, isVirtualKey);
}

class SimulateMouseEvent
{
	INPUT mInput;

  public:
	static bool sEmulatedMouseActive;

	SimulateMouseEvent()
	{
		sEmulatedMouseActive = true;
	}

	~SimulateMouseEvent()
	{
		::Sleep(10);
		sEmulatedMouseActive = false;
	}

	void LeftClick()
	{
		if (!IsClickSafe())
			return;

		Init();
		mInput.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		Send();
		Sleep(50);
		mInput.mi.dwFlags = MOUSEEVENTF_LEFTUP;
		Send();
	}

	void RightClick()
	{
		if (!IsClickSafe())
			return;

		Init();
		mInput.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
		Send();
		Sleep(50);
		mInput.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
		Send();
	}

	void Wheel(int delta_times = 1)
	{
		Init();
		mInput.mi.dwFlags = MOUSEEVENTF_WHEEL;
		mInput.mi.mouseData = DWORD(delta_times >= 0 ? WHEEL_DELTA : -WHEEL_DELTA);

		delta_times = abs(delta_times);
		for (int i = 0; i < delta_times; i++)
		{
			Send();
			WaitThreads();
		}
	}

	// x and y are absolute pixel coordinates
	void Move(DWORD x, DWORD y)
	{
		Init();
		// http://msdn.microsoft.com/en-us/library/ms646273%28v=VS.85%29.aspx
		const DWORD screenX = (DWORD)GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const DWORD screenY = (DWORD)GetSystemMetrics(SM_CYVIRTUALSCREEN);
		// virtual desktop absolute
		const DWORD dx = (x * 65335) / screenX;
		const DWORD dy = (y * 65335) / screenY;
		mInput.mi.dx = (LONG)dx;
		mInput.mi.dy = (LONG)dy;
		mInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
		Send();
		Sleep(100);
	}

	// x and y are number of pixels to move relative to current cursor position
	// scale with dpi [case: 110531]
	void MoveRelative(DWORD x, DWORD y)
	{
		Init();
		mInput.mi.dwFlags = MOUSEEVENTF_MOVE;
		mInput.mi.dx = VsUI::DpiHelper::LogicalToDeviceUnitsX((int)x);
		mInput.mi.dy = VsUI::DpiHelper::LogicalToDeviceUnitsY((int)y);
		Send();
		Sleep(100);
	}

	void LeftDoubleClick()
	{
		LeftClick();
		Sleep(10);
		LeftClick();
	}

  private:
	void Init()
	{
		ZeroMemory(&mInput, sizeof(mInput));
		mInput.type = INPUT_MOUSE;
	}

	bool IsClickSafe();

	void Send()
	{
		UINT keysSent = SendInput(1, &mInput, sizeof(INPUT));
		if (keysSent != 1)
		{
			WTString msg;
			msg.WTFormat("Failed to SendInput: Mouse");
			throw WtException(msg);
		}
	}
};

#define DEFAULT_TYPING_DELAY 100
class Typomatic
{
	HWND m_hLastActive;
	BOOL m_AutoDelayNonCSyms;
	BOOL m_bInCSym;

  protected:
	DWORD m_TypingDelay;

  public:
	Typomatic()
	    : m_hLastActive(NULL), m_AutoDelayNonCSyms(TRUE), m_bInCSym(FALSE), m_TypingDelay(DEFAULT_TYPING_DELAY),
	      m_quit(FALSE)
	{
	}
	virtual ~Typomatic() = default;

	virtual void TypeString(LPCSTR codeUtf8, BOOL checkOk = TRUE)
	{
		CStringW code = MbcsToWide(codeUtf8, strlen_i(codeUtf8));
		for (int i = 0; code[i]; i++)
		{
			if (checkOk && !IsOK())
				return;
			switch (code[i])
			{
			case '<': {
				int ep;
				for (ep = 0; code[i + ep] && code[i + ep] != '>'; ep++)
					;
				ProcessControlCharacters(WTString(code.Mid(i, ep)));
				i += ep;
			}
			break;
			case '\r':
				if (code[i + 1] == '\n')
					i++;
				// Fall through
			case '\n':
				SimulateKeyPress(VK_RETURN, TRUE);
				if (m_TypingDelay)
					::Sleep(m_TypingDelay);
				break;
			case '\\':
				if (code[i + 1] == '<')
					i++;
				else if (code[i + 1] == '\\') // Allow escaping of back slash
					i++;
				else if (code[i + 1] == '\r' || code[i + 1] == '\n')
				{
					for (++i; code[i]; i++)
					{
						if (!wt_isspace(code[i + 1]))
							break;
					}
					continue;
				}
			default: {
				if (m_AutoDelayNonCSyms)
				{
					if (ISCSYM(code[i]))
						m_bInCSym = TRUE;
					else
					{
						if (m_bInCSym)
						{
							HWND hFoc = ::GetFocus();
							if (hFoc && g_currentEdCnt && hFoc == (HWND)*g_currentEdCnt)
								WaitThreads();
						}
						m_bInCSym = FALSE;
					}
				}
				SimulateKeyPress(code[i]);
				if (m_TypingDelay == DEFAULT_TYPING_DELAY && (code[i] == '.' || code[i] == ':' || code[i] == '>'))
				{
					// Do a waitThreads after ., ->, and :: for test consistancy, if no typing delay specified.
					// If not, when sending "foo.bar", VA's listbox will detect(PeekMessage) the pending "bar" and cause
					// inconsistent behavior.
					WaitThreads();
				}
			}
				// Use delay between all key events, not just character strings
				if (m_TypingDelay)
					::Sleep(m_TypingDelay);
			}
		}
	}

	BOOL IsOK();

  protected:
	BOOL m_quit;

	void ProcessMouseCommand(WTString controlTag)
	{
		WTString cmdParams;
		WTString mouseCmd = controlTag.Mid(strlen_i("<Mouse:"));
		if (mouseCmd.IsEmpty())
			return;

		const int pos = mouseCmd.Find(':');
		if (-1 != pos)
		{
			cmdParams = mouseCmd.Mid(pos + 1);
			cmdParams.MakeLower();
			mouseCmd = mouseCmd.Left(pos);
		}

		mouseCmd.MakeLower();

		SimulateMouseEvent e;
		if (mouseCmd == "leftclick" || mouseCmd == "click")
			e.LeftClick();
		else if (mouseCmd == "rightclick")
			e.RightClick();
		else if (mouseCmd == "shiftrightclick")
		{
			KeyPressSimulator shiftHolder((CHAR)VK_SHIFT, TRUE);
			Sleep(50);
			e.RightClick();
			Sleep(50);
		}
		else if (mouseCmd == "ctrlleftclick" || mouseCmd == "ctrlclick")
		{
			KeyPressSimulator ctrlHolder((CHAR)VK_CONTROL, TRUE);
			Sleep(50);
			e.LeftClick();
			Sleep(50);
		}
		else if (mouseCmd == "altleftclick" || mouseCmd == "altclick")
		{
			KeyPressSimulator altHolder((CHAR)VK_MENU, TRUE);
			Sleep(50);
			e.LeftClick();
			Sleep(50);
		}
		else if (mouseCmd == "doubleclick" || mouseCmd == "leftdoubleclick")
			e.LeftDoubleClick();
		else if (mouseCmd == "move" || mouseCmd == "moveabsolute" || mouseCmd == "moveabs")
		{
			int x = 0, y = 0;
			if (cmdParams == "curpos")
			{
				_ASSERTE(g_currentEdCnt);
				CPoint pt(0, 0);
				if (g_currentEdCnt)
				{
					uint curPos = g_currentEdCnt->CurPos();
					pt = g_currentEdCnt->GetCharPosThreadSafe(curPos);
					g_currentEdCnt->vClientToScreen(&pt);
				}

				x = pt.x;
				y = pt.y;
			}
			else
				sscanf(cmdParams.c_str(), "%d,%d", &x, &y);

			_ASSERTE(x || y);
			e.Move((DWORD)x, (DWORD)y);
		}
		else if (mouseCmd == "moverelative" || mouseCmd == "moverel")
		{
			int x = 0, y = 0;
			sscanf(cmdParams.c_str(), "%d,%d", &x, &y);
			_ASSERTE(x || y);
			e.MoveRelative((DWORD)x, (DWORD)y);
		}
		else if (mouseCmd == "wheel")
		{
			int delta = _ttoi(cmdParams.c_str());
			_ASSERTE(delta);
			e.Wheel(delta);
		}
		else if (mouseCmd == "ctrlwheel")
		{
			int delta = _ttoi(cmdParams.c_str());
			_ASSERTE(delta);
			KeyPressSimulator ctrl_key(VK_CONTROL, TRUE);
			e.Wheel(delta);
		}
		else if (mouseCmd == "ctrlshiftwheel")
		{
			int delta = _ttoi(cmdParams.c_str());
			_ASSERTE(delta);
			KeyPressSimulator ctrl_key(VK_CONTROL, TRUE);
			KeyPressSimulator shift_key(VK_SHIFT, TRUE);
			e.Wheel(delta);
		}
		else
		{
			WTString msg;
			msg.WTFormat("Unknown Mouse command %s", mouseCmd.c_str());
			throw WtException(msg);
		}
	}

	virtual void ProcessKeyboardLayoutHexKeys(WTString controlTag)
	{
		WTString cmd = controlTag.Mid(strlen_i("<KLHexKeys:"));

		if (cmd.IsEmpty())
			return;

		token2 t = cmd;
		WTString layout = t.read(':');

		HKL hkl = (HKL)(uintptr_t)_tcstoul(layout.c_str(), nullptr, 16);
		KeyboardLayoutToggle kl_toggle(hkl);

		for (WTString key = t.read(':'); !key.IsEmpty(); key = t.read(":>"))
		{
			auto vk = (WCHAR)_tcstoul(key.c_str(), nullptr, 16);
			SimulateKeyPress(vk, TRUE);

			if (m_TypingDelay)
				::Sleep(m_TypingDelay);
		}
	}

	virtual void ProcessControlCharacters(WTString controlTag)
	{
		if (controlTag.FindNoCase("<Mouse:") == 0)
		{
			ProcessMouseCommand(controlTag);
			return;
		}
		else if (controlTag.FindNoCase("<KLHexKeys:") == 0)
		{
			ProcessKeyboardLayoutHexKeys(controlTag);
			return;
		}

		// <ALT>, or <CTRL_ALT>, <ALT+o>...
		token2 t = controlTag[0] == '<' ? controlTag.Mid(1) : controlTag; // skip '<'
		token2 modKeys = t.read(':');
		int repeatCount = atoi(modKeys.Str().c_str());
		if (repeatCount)
			modKeys = t.read(':');
		WTString keys = t.read(':');

		// Issue KeyDown of the mod keys with "new KeyPressSimulator"
		std::list<KeyPressSimulator*> altKeys;
		while (modKeys.GetLength() && IsOK())
		{
			token2 modKey = modKeys.read("+_");
			int curVKey = 0;

#define IF_MAP_NAME_TO_KEY(str, vkey)                                                                                  \
	if (StrStrI(modKey.WTString::c_str(), str))                                                                        \
	curVKey = vkey

			if (modKey.GetLength() == 1)
				curVKey = toupper(modKey[0]);
			else
				IF_MAP_NAME_TO_KEY("CTRL", VK_CONTROL);
			else IF_MAP_NAME_TO_KEY("ALT", VK_MENU);
			else IF_MAP_NAME_TO_KEY("SHIFT", VK_SHIFT);
			else IF_MAP_NAME_TO_KEY("ESC", VK_ESCAPE);
			else IF_MAP_NAME_TO_KEY("PRIOR", VK_PRIOR);
			else IF_MAP_NAME_TO_KEY("NEXT", VK_NEXT);
			else IF_MAP_NAME_TO_KEY("END", VK_END);
			else IF_MAP_NAME_TO_KEY("HOME", VK_HOME);
			else IF_MAP_NAME_TO_KEY("UP", VK_UP);
			else IF_MAP_NAME_TO_KEY("DOWN", VK_DOWN);
			else IF_MAP_NAME_TO_KEY("LEFT", VK_LEFT);
			else IF_MAP_NAME_TO_KEY("RIGHT", VK_RIGHT);
			else IF_MAP_NAME_TO_KEY("INSERT", VK_INSERT);
			else IF_MAP_NAME_TO_KEY("DEL", VK_DELETE);
			else IF_MAP_NAME_TO_KEY("BACK", VK_BACK);
			else IF_MAP_NAME_TO_KEY("LWIN", VK_LWIN);
			else IF_MAP_NAME_TO_KEY("RWIN", VK_RWIN);
			else IF_MAP_NAME_TO_KEY("TAB", VK_TAB);
			else IF_MAP_NAME_TO_KEY("CLEAR", VK_CLEAR);
			else IF_MAP_NAME_TO_KEY("SPACE", VK_SPACE);
			else IF_MAP_NAME_TO_KEY("APPS", VK_APPS);
			else IF_MAP_NAME_TO_KEY("F10", VK_F10);
			else IF_MAP_NAME_TO_KEY("F11", VK_F11);
			else IF_MAP_NAME_TO_KEY("F12", VK_F12);
			else IF_MAP_NAME_TO_KEY("F1", VK_F1);
			else IF_MAP_NAME_TO_KEY("F2", VK_F2);
			else IF_MAP_NAME_TO_KEY("F3", VK_F3);
			else IF_MAP_NAME_TO_KEY("F4", VK_F4);
			else IF_MAP_NAME_TO_KEY("F5", VK_F5);
			else IF_MAP_NAME_TO_KEY("F6", VK_F6);
			else IF_MAP_NAME_TO_KEY("F7", VK_F7);
			else IF_MAP_NAME_TO_KEY("F8", VK_F8);
			else IF_MAP_NAME_TO_KEY("F9", VK_F9);
			else if (::StrStrI(modKey.WTString::c_str(), "ENTER") || ::StrStrI(modKey.WTString::c_str(), "RETURN"))
			{
				curVKey = VK_RETURN;
				// potential fix for intermittent failure to exit dialogs?
				::Sleep(100);
			}
			else
			{
				WTString msg;
				msg.WTFormat("Unknown key %s", modKey.WTString::c_str());
				throw WtException(msg);
			}

			if (curVKey)
			{
				if (modKeys.more() || 1 > repeatCount)
					altKeys.push_back(new KeyPressSimulator((WCHAR)curVKey, TRUE));
				else
				{
					_ASSERTE(repeatCount);
					while (repeatCount--)
					{
						SimulateKeyPress((WCHAR)curVKey, TRUE);
						if (m_TypingDelay)
							::Sleep(m_TypingDelay);

						if (!IsOK())
							break;
					}
				}

				if (VK_RETURN == curVKey)
				{
					// potential fix for intermittent failure to exit dialogs?
					::Sleep(100);
				}
			}

			if (m_TypingDelay)
				::Sleep(m_TypingDelay);
		}

		// Send the "+keys" if any
		if (keys.GetLength() && IsOK())
		{
			_ASSERTE(1 > repeatCount && "repeat count not supported for +keys");
			CStringW wkeys = keys.Wide();
			UnicodeHelper::MakeUpper(wkeys);
			for (int i = 0; i < wkeys.GetLength(); i++)
			{
				SimulateKeyPress(wkeys[i], TRUE);
				if (m_TypingDelay)
					Sleep(m_TypingDelay);
			}
		}

		_ASSERTE((1 > repeatCount && "repeat count was not used") || m_quit);

		// Issue KeyUp of the mod keys with "delete"
		std::list<KeyPressSimulator*>::const_reverse_iterator it;
		for (it = altKeys.rbegin(); it != altKeys.rend(); ++it)
		{
			delete *it;
			if (m_TypingDelay)
				Sleep(m_TypingDelay);
		}
		altKeys.clear();
	}
};
