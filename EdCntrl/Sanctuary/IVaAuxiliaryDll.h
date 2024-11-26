#pragma once

#include "IVaAccessFromAux.h"
#include "ISanctuaryClient.h"

//
// this interface is used to manage access to the aux dll from va_x.dll
__interface IVaAuxiliaryDll
{
	void VaUnloading();

	void InitSanctuaryClient(IVaAccessFromAux * va);
	SanctuaryClientPtr GetSanctuaryClient();
};
