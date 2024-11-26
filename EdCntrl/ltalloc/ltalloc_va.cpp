#include <cstdlib>
#include "ltalloc_va.h"

#ifndef _M_ARM64
#include "ltalloc.h"
#include "utils3264.h"

class LTAllocVA final : public ILTAllocVA
{
  public:
	~LTAllocVA() = default;

	void* ltmalloc(size_t size) override
	{
		return ::ltmalloc(size);
	}
	void ltfree(void* p) override
	{
		::ltfree(p);
	}
	void* ltrealloc(void* p, size_t size) override
	{
		return ::ltrealloc(p, size);
	}
	void* ltcalloc(size_t elems, size_t size) override
	{
		return ::ltcalloc(elems, size);
	}
	void* ltmemalign(size_t align, size_t size) override
	{
		return ::ltmemalign(align, size);
	}
	void ltsqueeze(size_t padsz = 0) override
	{
		(void)padsz; /*::ltsqueeze(padsz);*/
	}
	size_t ltmsize(void* p) override
	{
		return ::ltmsize(p);
	}

	//	void *ltmalloc(size_t size) override { return nullptr; }
	//	void ltfree(void *p) override {  }
	//	void *ltrealloc(void *p, size_t size) override { return nullptr; }
	//	void *ltcalloc(size_t elems, size_t size) override { return nullptr; }
	//	void *ltmemalign(size_t align, size_t size) override { return nullptr; }
	//	void ltsqueeze(size_t padsz = 0) override { }
	//	size_t ltmsize(void *p) override { return 0; }
};
#else
class LTAllocVA final : public ILTAllocVA
{
  public:
	~LTAllocVA() = default;

	void* ltmalloc(size_t size) override
	{
		return ::malloc(size);
	}
	void ltfree(void* p) override
	{
		::free(p);
	}
	void* ltrealloc(void* p, size_t size) override
	{
		return ::realloc(p, size);
	}
	void* ltcalloc(size_t elems, size_t size) override
	{
		return ::calloc(elems, size);
	}
	void* ltmemalign(size_t align, size_t size) override
	{
		return ::_aligned_malloc(size, align);
	}
	void ltsqueeze(size_t padsz = 0) override
	{
		(void)padsz;
	}
	size_t ltmsize(void* p) override
	{
		return ::_msize(p);
	}
};
#endif

void* get_iltava()
{
	static LTAllocVA ltalloc;
	return &ltalloc;
}
