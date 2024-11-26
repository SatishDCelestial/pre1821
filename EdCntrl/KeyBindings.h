#pragma once

#include "WTString.h"

WTString GetBindingTip(LPCSTR cmsStr, LPCSTR vc6Binding = NULL, BOOL inParens = TRUE);
INT_PTR CheckForKeyBindingUpdate(bool forcePrompt = false);
bool ShowVAKeyBindingsDialog();
DWORD QueryStatusVaKeyBindingsDialog();
CStringW GetListOfKeyBindingsResourcesForAST();
