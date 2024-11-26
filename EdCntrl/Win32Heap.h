#pragma once

#include "SpinCriticalSection.h"
#include "Lock.h"
#include "LOG.H"

#ifdef _DEBUG
#define VaWin32Heap_MemStats
#define VaWin32Heap_MemDbg
#endif

#if defined(VaWin32Heap_MemDbg)

#if !defined(VaWin32Heap_MemStats)
#error "VaWin32Heap_MemDbg requires VaWin32Heap_MemStats"
#endif

inline int CheckBytes(unsigned char* pb, unsigned char bCheck, size_t nSize)
{
	while (nSize--)
	{
		if (*pb++ != bCheck)
		{
			return FALSE;
		}
	}
	return TRUE;
}
#endif

// based on ATL CWin32Heap with a bit of dbgcrt tossed in
class Win32Heap
{
  public:
#ifndef VaWin32Heap_MemStats
	Win32Heap() throw()
	{
	}
	Win32Heap(size_t nInitialSize, const char* name, DWORD dwFlags = 0, size_t nMaxSize = 0) throw()
#else
	Win32Heap(uint32_t nNoMansLandSize = 4) throw()
	    : nNoMansLandSize(nNoMansLandSize)
	{
	}
	Win32Heap(size_t nInitialSize, const char* name, DWORD dwFlags = 0, size_t nMaxSize = 0, uint32_t nNoMansLandSize = 4) throw()
	    : nNoMansLandSize(nNoMansLandSize)
#endif
	{
		Create(nInitialSize, name, dwFlags, nMaxSize);
	}

	virtual ~Win32Heap() throw()
	{
		Close();
	}

	Win32Heap &operator =(const Win32Heap &) = delete;

	void Attach(_In_ HANDLE hHeap, _In_ bool bTakeOwnership = false) throw()
	{
		ATLASSERT(hHeap != NULL);
		ATLASSUME(m_hHeap == NULL && "Close current heap first");
		ATLASSUME(m_sharedHeap == NULL && "Close current heap first");

		m_hHeap = hHeap;
		m_bOwnHeap = bTakeOwnership;
	}

	void Attach(_In_ Win32Heap& hHeap) throw()
	{
		ATLASSUME(m_hHeap == NULL && "Close current heap first");

		m_sharedHeap = &hHeap;
		_ASSERTE(hHeap.IsCreated());
		m_hHeap = hHeap.m_hHeap;
		m_bOwnHeap = false;
	}

	void Create(size_t nInitialSize, const char* name, DWORD dwFlags = 0, size_t nMaxSize = 0)
	{
		_ASSERTE(!m_hHeap);
		_ASSERTE(!m_sharedHeap);
		ATLASSERT(!(dwFlags & HEAP_GENERATE_EXCEPTIONS));
		m_bOwnHeap = true;
		m_hHeap = ::HeapCreate(dwFlags, nInitialSize, nMaxSize);
		if (m_hHeap == NULL)
		{
			VALOGERROR("ERROR: HeapCreate failed\n");
			AtlThrowLastWin32();
		}
		m_heapName = name;

		vCatLog("LowLevel", "PrivateH: %s - %p", name, m_hHeap);
	}

	/*__declspec(allocator)*/ _Ret_maybenull_ _Post_writable_byte_size_(nBytes) byte* Alloc(_In_ size_t nBytes) throw()
	{
		byte* p;

		if (m_sharedHeap)
			p = m_sharedHeap->Alloc(nBytes);
		else
		{
#ifdef VaWin32Heap_MemStats
			p = (byte*)::HeapAlloc(m_hHeap, 0, nBytes + (nNoMansLandSize * 2));
			if (p)
			{
#ifdef VaWin32Heap_MemDbg
				memset(p, _bNoMansLandFill, nNoMansLandSize);
#endif
				p += nNoMansLandSize;
#ifdef VaWin32Heap_MemDbg
				memset((void*)(p + nBytes), _bNoMansLandFill, nNoMansLandSize);

				/* fill data with silly value (but non-zero) */
				memset((void*)p, _bCleanLandFill, nBytes);
#endif
			}
#else
			p = (byte*)::HeapAlloc(m_hHeap, 0, nBytes);
#endif
		}

#ifdef VaWin32Heap_MemStats
		if (p)
		{
			InterlockedIncrement(&mAllocCallCount);
			InterlockedIncrement(&mActiveAllocs);
			InterlockedExchangeAdd(&mCurAllocBytes, (LONG)nBytes);
			if (mCurAllocBytes > mMaxCumulativeBytes)
				mMaxCumulativeBytes = mCurAllocBytes;
			if (nBytes > mMaxSingleAllocBytes)
				mMaxSingleAllocBytes = nBytes;
		}
#endif
		if (!p)
		{
			InterlockedIncrement(&mAllocationFailures);
			static bool once = true;
			if (once)
			{
				once = false;
				::ErrorBox("Visual Assist memory allocation failed.  Features may not work correctly.");
			}
		}

		return p;
	}

	/*__declspec(allocator)*/ _Ret_maybenull_ _Post_writable_byte_size_(nBytes) virtual void* Realloc(
	    _In_opt_ void* p, _In_ size_t nBytes) throw()
	{
		if (p == NULL)
		{
			return (Alloc(nBytes));
		}

		if (nBytes == 0)
		{
			Free(p);
			return NULL;
		}

		if (m_sharedHeap)
			return m_sharedHeap->Realloc(p, nBytes);

#ifdef VaWin32Heap_MemStats
		long oldSize = (long)GetSize(p);
#ifdef VaWin32Heap_MemDbg
		if (!CheckBytes((byte*)p - nNoMansLandSize, _bNoMansLandFill, nNoMansLandSize))
			_ASSERTE(!"HEAP CORRUPTION DETECTED: VA Win32Heap detected that someone wrote to memory before start of "
			          "heap buffer.");

		if (!CheckBytes((byte*)p + oldSize, _bNoMansLandFill, nNoMansLandSize))
			_ASSERTE(!"HEAP CORRUPTION DETECTED: VA Win32Heap detected that someone wrote to memory after end of heap "
			          "buffer.");
#endif

		byte* pb = (byte*)::HeapReAlloc(m_hHeap, 0, (byte*)p - nNoMansLandSize, nBytes + (nNoMansLandSize * 2));
		if (pb)
		{
#ifdef VaWin32Heap_MemDbg
			memset(pb, _bNoMansLandFill, nNoMansLandSize);
#endif
			pb += nNoMansLandSize;
#ifdef VaWin32Heap_MemDbg
			memset((void*)(pb + nBytes), _bNoMansLandFill, nNoMansLandSize);
#endif

			InterlockedIncrement(&mAllocCallCount);
			InterlockedExchangeAdd(&mCurAllocBytes, LONG(nBytes - oldSize));
			if (mCurAllocBytes > mMaxCumulativeBytes)
				mMaxCumulativeBytes = mCurAllocBytes;
			if (nBytes > mMaxSingleAllocBytes)
				mMaxSingleAllocBytes = nBytes;
		}
		return pb;
#else
		void* res = ::HeapReAlloc(m_hHeap, 0, p, nBytes);
		if (!res)
			InterlockedIncrement(&mAllocationFailures);
		return res;
#endif
	}

	void Free(_In_opt_ void* p) throw()
	{
		if (p != NULL)
		{
#ifdef VaWin32Heap_MemStats
			InterlockedDecrement(&mActiveAllocs);
			LONG size = (LONG)GetSize(p);
			InterlockedExchangeAdd(&mCurAllocBytes, -size);
#endif
			if (m_sharedHeap)
			{
				m_sharedHeap->Free(p);
			}
			else
			{
#ifdef VaWin32Heap_MemStats
#ifdef VaWin32Heap_MemDbg
				if (!CheckBytes((byte*)p - nNoMansLandSize, _bNoMansLandFill, nNoMansLandSize))
					_ASSERTE(!"HEAP CORRUPTION DETECTED: VA Win32Heap detected that someone wrote to memory before "
					          "start of heap buffer.");

				if (!CheckBytes((byte*)p + size, _bNoMansLandFill, nNoMansLandSize))
					_ASSERTE(!"HEAP CORRUPTION DETECTED: VA Win32Heap detected that someone wrote to memory after end "
					          "of heap buffer.");
#endif

				p = (byte*)p - nNoMansLandSize;
#ifdef VaWin32Heap_MemDbg
				memset(p, _bDeadLandFill, size_t(size + (nNoMansLandSize * 2)));
#endif
#endif
				BOOL bSuccess = ::HeapFree(m_hHeap, 0, p);
				ATLASSERT(bSuccess);
				std::ignore = bSuccess;
			}
		}
	}

	size_t GetSize(_Inout_ void* p) throw()
	{
		if (m_sharedHeap)
			return m_sharedHeap->GetSize(p);
		else
		{
#ifdef VaWin32Heap_MemStats
			size_t sz = ::HeapSize(m_hHeap, 0, (byte*)p - nNoMansLandSize);
			sz -= (nNoMansLandSize * 2);
			return sz;
#else
			return (::HeapSize(m_hHeap, 0, p));
#endif
		}
	}

	void AssertEmpty() const throw()
	{
#ifdef VaWin32Heap_MemStats
		_ASSERTE(!mActiveAllocs && !mCurAllocBytes);
#endif
	}

	void Close() throw()
	{
		AssertEmpty();
		if (m_hHeap != NULL && m_bOwnHeap)
		{
			BOOL bSuccess;

			bSuccess = ::HeapDestroy(m_hHeap);
			ATLASSERT(bSuccess);
		}

		m_hHeap = nullptr;
		m_bOwnHeap = false;
		m_sharedHeap = nullptr;
	}

	bool IsCreated() const throw()
	{
		return !!m_hHeap;
	}

#ifdef VaWin32Heap_MemStats
	long GetCurAllocBytes() const
	{
		return mCurAllocBytes;
	}
#endif

  private:
	HANDLE m_hHeap = nullptr;
	Win32Heap* m_sharedHeap = nullptr;
	const char* m_heapName = nullptr;
	bool m_bOwnHeap = false;
	size_t mAllocationFailures = 0;
#ifdef VaWin32Heap_MemStats
	long mCurAllocBytes = 0;         // total outstanding allocations
	size_t mMaxSingleAllocBytes = 0; // most allocated at a single time
	long mMaxCumulativeBytes = 0;    // allocation high water mark
	size_t mAllocCallCount = 0;      // count of calls to Alloc
	size_t mActiveAllocs = 0;        // count of calls to Alloc less calls to Free

#ifdef VaWin32Heap_MemDbg
	static const unsigned char _bNoMansLandFill = 0xFD; /* fill no-man's land with this */
	static const unsigned char _bDeadLandFill = 0xDD;   /* fill free objects with this */
	static const unsigned char _bCleanLandFill = 0xCD;  /* fill new objects with this */
#endif
	const uint32_t nNoMansLandSize /*= 4*/;
#endif
};
