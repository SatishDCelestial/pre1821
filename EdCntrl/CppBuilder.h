#pragma once

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "../CppBuilder/IRadStudioHost.h"

// extern IRadStudioHost* gRadStudioHost;
extern IRadStudioHostEx* gRadStudioHost; // temporary while working out host interface

#endif
