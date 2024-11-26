#pragma once

#include "Win32Heap.h"

// [case: 92495]
// This class is used to create a string derived from CString that uses an
// independent / private heap.
// see usage in FileFinder::FileFinderStrTrait and FileIdManager::FileIdStrTrait.
// It is cloned from CAfxStringMgr but uses a member heap instead of CRT malloc/free.

class PrivateHeapStringMgr : public IAtlStringMgr
{
  public:
	PrivateHeapStringMgr(size_t initialMbSize, const char* heapName)
	{
		m_nil.SetManager(this);
		mHeap.Create(initialMbSize * 1024 * 1024, heapName);
	}
	virtual ~PrivateHeapStringMgr() = default;

	// 	PrivateHeapStringMgr()
	// 	{
	// 		m_nil.SetManager(this);
	// 		mHeap.Attach(::GetProcessHeap());
	// 	}

	// IAtlStringMgr
  public:
	virtual CStringData* Allocate(int nChars, int nCharSize) throw()
	{
		size_t nTotalSize;
		CStringData* pData;
		size_t nDataBytes;

		ASSERT(nCharSize > 0);

		if (nChars < 0)
		{
			ASSERT(FALSE);
			return NULL;
		}

		nDataBytes = size_t((nChars + 1) * nCharSize);
		nTotalSize = sizeof(CStringData) + nDataBytes;
		pData = (CStringData*)mHeap.Alloc(nTotalSize);
		if (pData == NULL)
			return NULL;
		pData->pStringMgr = this;
		pData->nRefs = 1;
		pData->nAllocLength = nChars;
		pData->nDataLength = 0;

		return pData;
	}

	virtual void Free(CStringData* pData) throw()
	{
		mHeap.Free(pData);
	}

	virtual CStringData* Reallocate(CStringData* pData, int nChars, int nCharSize) throw()
	{
		CStringData* pNewData;
		size_t nTotalSize;
		size_t nDataBytes;

		ASSERT(nCharSize > 0);

		if (nChars < 0)
		{
			ASSERT(FALSE);
			return NULL;
		}

		nDataBytes = size_t((nChars + 1) * nCharSize);
		nTotalSize = sizeof(CStringData) + nDataBytes;
		pNewData = (CStringData*)mHeap.Realloc(pData, nTotalSize);
		if (pNewData == NULL)
		{
			return NULL;
		}
		pNewData->nAllocLength = nChars;

		return pNewData;
	}

	virtual CStringData* GetNilString() throw()
	{
		m_nil.AddRef();
		return &m_nil;
	}

	virtual IAtlStringMgr* Clone() throw()
	{
		return this;
	}

#if 0
	void RecycleHeap()
	{
		MessageBox(NULL, "About to destroy heap.", "sean", 0);
		mHeap.Close();
		MessageBox(NULL, "Heap destroyed.  About to create heap.", "sean", 0);
		mHeap.Create(15 * 1024 * 1024, "TestingHeap");
		MessageBox(NULL, "15mb heap created.", "sean", 0);
	}
#endif

  protected:
	CNilStringData m_nil;
	Win32Heap mHeap;
};
