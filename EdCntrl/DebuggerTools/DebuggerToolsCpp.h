#pragma once

__interface IVaDebuggerToolService;

DWORD IsStepIntoSkipped(const wchar_t* functionname);
IVaDebuggerToolService* VaDebuggerToolService(int managed);
void ClearDebuggerGlobals(bool clearDbgService = false);
