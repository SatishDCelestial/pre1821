#include "VaMessages.h"

// this is used in VA, the VSNetaddin and the package
inline HWND VAGetFocus()
{
	HWND hFoc = ::GetFocus();
	HWND h = (HWND)::SendMessage(hFoc, WM_VA_WPF_GETFOCUS, 0, 0);
	if (h)
		return h;
	return hFoc;
}
