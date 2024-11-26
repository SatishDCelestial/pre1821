#pragma once

struct ILTAllocVA
{
	virtual ~ILTAllocVA() = default;

	virtual void* ltmalloc(size_t size) = 0;
	virtual void ltfree(void* p) = 0;
	virtual void ltsqueeze(size_t padsz = 0) = 0;

#ifndef NO_LTALLOC
	virtual void* ltrealloc(void* p, size_t size) = 0;
	virtual void* ltcalloc(size_t elems, size_t size) = 0;
	virtual void* ltmemalign(size_t align, size_t size) = 0;
	virtual size_t ltmsize(void* p) = 0;
#endif
};

#ifndef NO_LTALLOC
#include "..\..\vaIPC\vaIPC\dllexport.h"
DLLEXPORT void* get_iltava();
#else
void* get_iltava();
#endif

#include <assert.h>
inline ILTAllocVA* get_ltalloc()
{
	auto ret = (ILTAllocVA*)get_iltava();
	assert(ret);
	return ret;
}
