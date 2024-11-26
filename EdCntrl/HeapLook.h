#pragma once

#include <atlbase.h>
#include "DevShellAttributes.h"

void CheckHeapLookaside()
{
	if (!gShellAttr->RequiresHeapLookaside())
		return;

	CString key(CString("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\"));
	key += gShellAttr->GetExeName();
	CRegKey hK;
	LSTATUS err = hK.Open(HKEY_LOCAL_MACHINE, key, KEY_WRITE | KEY_READ);
	if (err == ERROR_SUCCESS)
	{
		// application key exists - don't change any values
		hK.Close();
		return;
	}

	// app key does not exist - set it up
	err = hK.Create(HKEY_LOCAL_MACHINE, key, REG_NONE, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ);
	if (err == ERROR_SUCCESS)
	{
#if _MSC_VER <= 1200
		hK.SetValue("1", "DisableHeapLookaside");
#else
		hK.SetStringValue("DisableHeapLookaside", "1");
#endif
		hK.Close();
	}
}
