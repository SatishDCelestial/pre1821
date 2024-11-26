#pragma once

#include <crtdbg.h>

#ifdef _DEBUG
#define ASSERT_ONCE(cond)                                                                                              \
	{                                                                                                                  \
		static BOOL once = TRUE;                                                                                       \
		if (once && !(cond))                                                                                           \
		{                                                                                                              \
			once = FALSE;                                                                                              \
			_ASSERTE(cond);                                                                                            \
		}                                                                                                              \
	}
#else
#define ASSERT_ONCE(cond)
#endif // _DEBUG
