#pragma once

//
// Aux dll uses this interface to talk to va_x.dll
__interface IVaAccessFromAux
{
	// VaDirs
	LPCWSTR GetDllDir();
	LPCWSTR GetUserDir();

	void GetBuildDate(int& buildYear, int& buildMon, int& buildDay);

	void LogStr(const LPCSTR txt);
};
