#include "stdafxed.h"
#include <locale.h>
#include "wtstring.h"
#include "bc/cstring.h"
#include "settings.h"
#include "wtcsym.h"
#include "token.h"
#include "file.h"
#include "TokenW.h"
#include "StringUtils.h"
#include "Mparse.h"
#include "CodeGraph.h"
#include "MenuXP/Tools.h"
#include <ranges>
#include "vaIPC\vaIPC\common\string_utils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(8) // the default
#endif

#ifdef _DEBUG
#define VaWTString_MemStats
#endif

// Disabling shared strings refs (disabling copy on write) will break VA.
// Many pointers into WTStrings are passed around and are dependent upon
// lifetime affects of shared refs.
// #define NO_SHARED_REFS

#if _MSC_VER > 1200
using std::ios;
using std::istream;
#endif

using OWL::string;
using OWL::TRegexp;

const WTString DB_SEP_STR(DB_SEP_CHR);
const WTString DB_SCOPE_PREPROC = DBColonToSepStr(":PP::");
const WTString kUnnamed(DB_SEP_STR + "unnamed"); // dependent on init of DB_SEP_STR

#ifdef VaWTString_MemStats
std::atomic_uint64_t gWtstrCurAllocBytes = 0;      // total outstanding allocations
std::atomic_uint64_t gWtstrMaxCumulativeBytes = 0; // allocation high water mark
std::atomic_uint64_t gWtstrActiveAllocs = 0;       // count of calls to Alloc less calls to Free
std::atomic_uint64_t gWtstrActiveAllocs_64 = 0;
std::atomic_uint64_t gWtstrActiveAllocs_128 = 0;
std::atomic_uint64_t gWtstrActiveAllocs_256 = 0;
std::atomic_uint64_t gWtstrTotalAllocs = 0;
std::atomic_uint64_t gWtstrTotalAllocs_64 = 0;
std::atomic_uint64_t gWtstrTotalAllocs_128 = 0;
std::atomic_uint64_t gWtstrTotalAllocs_256 = 0;
std::atomic_uint64_t gWtstrReallocs = 0;
std::atomic_uint64_t gWtstrCopies = 0;
std::atomic_uint64_t gWtstrClashes = 0;
std::atomic_uint64_t gWtstrAllocsRecycled = 0;

volatile uintptr_t gWtstrThread = _beginthread([](void *) {
	while (true)
	{
		static uint64_t no = 0;
		std::this_thread::sleep_for(std::chrono::seconds(5));
		++no;
		::OutputDebugStringA(WTString().WTFormat("FEC: Mem stat #%llu. active_allocs(%llu:%llu:%llu:%llu) total_allocs(%llu:%llu:%llu:%llu) reallocs(%llu) copies(%llu) alloc_mb(%llu, %llu max) clashed(%llu) recycled(%llu)", no, gWtstrActiveAllocs_64.load(), gWtstrActiveAllocs_128.load(), gWtstrActiveAllocs_256.load(), gWtstrActiveAllocs.load() - gWtstrActiveAllocs_64.load() - gWtstrActiveAllocs_128.load() - gWtstrActiveAllocs_256.load(), gWtstrTotalAllocs_64.load(), gWtstrTotalAllocs_128.load(), gWtstrTotalAllocs_256.load(), gWtstrTotalAllocs.load() - gWtstrTotalAllocs_64.load() - gWtstrTotalAllocs_128.load() - gWtstrTotalAllocs_256.load(), gWtstrReallocs.load(), gWtstrCopies.load(), gWtstrCurAllocBytes.load() / 1048576, gWtstrMaxCumulativeBytes.load() / 1048576, gWtstrClashes.load(), gWtstrAllocsRecycled.load()).c_str());
	}
}, 0, nullptr);
#endif

// from vc/mfc/src/afximpl.h
// determine number of elements in an array (not bytes)
#if !defined(_countof)
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

// from afxpriv.h and defined in dllinit.cpp
extern int AFXAPI AfxLoadString(UINT nIDS, LPTSTR lpszBuf, UINT nMaxBuf /*= 256*/);
#include <mbctype.h>
bool WTString::ReadFile(const CStringW& file, int maxAmt /* = -1*/, bool forceUtf8 /*= false*/)
{
	DWORD err;
	int retry = 50;
again:
	HANDLE hFile = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                           FILE_ATTRIBUTE_NORMAL, NULL);
	if (!hFile || hFile == INVALID_HANDLE_VALUE)
	{
		err = GetLastError();
		if (retry && err == 0x20)
		{
			retry--;
			vLog("WARN WTS::ReadFile: retry read %s %ld", (LPCTSTR)CString(file), err);
			Sleep(50);
			goto again;
		}
		vLogUnfiltered("ERROR WTS::ReadFile: reading %s %ld", (LPCTSTR)CString(file), err);
		Empty();
		return false;
	}
	DWORD fsLow, fsHi;
	fsLow = GetFileSize(hFile, &fsHi);
	if (fsLow == 0xFFFFFFFF && (err = GetLastError()) != NO_ERROR)
	{
		CloseHandle(hFile);
		vLogUnfiltered("ERROR WTS::ReadFile: 2 reading %s %ld", (LPCTSTR)CString(file), err);
		_ASSERTE(!"ERROR WTS::ReadFile: 2");
		Empty();
		return false;
	}
	else if (fsHi)
	{
		CloseHandle(hFile);
		vLogUnfiltered("ERROR WTS::ReadFile: reading huge file %s", (LPCTSTR)CString(file));
		_ASSERTE(!"ERROR WTS::ReadFile:");
		Empty();
		// return or read as much as we can??
		return false;
	}

	if (maxAmt != -1 && fsLow > (DWORD)maxAmt)
		fsLow = (uint)maxAmt;

	// might be reading fsLow CHARs or fsLow/2 WCHARs
	if (!AllocBeforeWrite((int)fsLow + 2))
	{
		Empty();
		vLogUnfiltered("ERROR WTS::ReadFile: alloc fail %s", (LPCTSTR)CString(file));
		_ASSERTE(!"ERROR WTS::ReadFile: alloc fail");
		return false;
	}

	if (::ReadFile(hFile, m_pchData, fsLow, &fsHi, NULL))
	{
		GetData()->nDataLength = (int)fsHi;
		m_pchData[fsHi] = '\0';
		m_pchData[fsHi + 1] = '\0'; // in case is wide
		CloseHandle(hFile);

		if (fsHi > 1 && m_pchData[0] == '\xfe' && m_pchData[1] == '\xff')
		{
			// UTF16 BE: turn into UTF16 LE by swapping bytes
			_swab(m_pchData, m_pchData, (int)fsHi);
		}

		// Added support for UTF_8 to fix Case 2511:Find References/Rename cannot find References
		// Byte Order Mark: http://msdn2.microsoft.com/en-us/library/ms776429.aspx
		// UTF_8, 0xefbbbf
		if (fsHi >= 3 && m_pchData[0] == '\xef' && m_pchData[1] == '\xbb' && m_pchData[2] == '\xbf')
		{
			// [case: 2511] [case: 43609] UTF8 w/ BOM
			WTString buf(&m_pchData[3], (int)fsHi - 3);
			(*this) = buf;
		}
		else if (fsHi > 1 && m_pchData[0] == '\xff' && m_pchData[1] == '\xfe')
		{
			// UNICODE file?
			// Assert UTF_16, little endian 0xfffe
			WTString buf(::WideToMbcs((LPCWSTR)&m_pchData[2], int(fsHi / 2) - 1));
			(*this) = buf;
		}
		else
		{
			DWORD cpBehavior = Psettings ? Psettings->mFileLoadCodePageBehavior : Settings::FLCP_AutoDetect;
			if (forceUtf8)
				cpBehavior = Settings::FLCP_ForceUtf8;
			if (Settings::FLCP_AutoDetect == cpBehavior)
			{
				if (::CanReadAsUtf8(&m_pchData[0]))
					cpBehavior = Settings::FLCP_ForceUtf8;
				else
					cpBehavior = Settings::FLCP_ACP;
			}

			_ASSERTE(cpBehavior != Settings::FLCP_AutoDetect);
			if (Settings::FLCP_ForceUtf8 == cpBehavior)
			{
				// 7-bit Ascii or utf8 w/o BOM
				// good to go as is
			}
			else
			{
				// not utf8, use ACP or user-defined codepage to convert to utf8 by way of CStringW
				CStringW tmp(::MbcsToWide(&m_pchData[0], GetLength(),
				                          int(Settings::FLCP_ACP == cpBehavior ? ::GetACP() : cpBehavior)));
				*this = tmp;
			}
		}
		return true;
	}
	else
	{
		err = GetLastError();
		Empty();
		vLogUnfiltered("ERROR WTS::ReadFile: 3 reading %s %ld", (LPCTSTR)CString(file), err);
		_ASSERTE(!"ERROR WTS::ReadFile: 3");
		CloseHandle(hFile);
		return false;
	}
}

void WTString::read_to_delim(istream& strm, char delim, int resizeInc)
{
	/*------------------------------------------------------------------------*/
	/*  Read up to an EOF, or a delimiting character, whichever comes         */
	/*  first.  The delimiter is not stored in the string,                    */
	/*  but is removed from the input stream.                                 */
	/*                                                                        */
	/*  Because we don't know how big a string to expect, we first read       */
	/*  as much as we can and then, if the EOF or null hasn't been            */
	/*  encountered, do a resize and keep reading.                            */
	/*------------------------------------------------------------------------*/
	char ch;
	if (!AllocBeforeWrite(resizeInc))
	{
		Empty();
		vLog("ERROR WTS::rtd: alloc fail");
		_ASSERTE(!"ERROR WTS::rtd");
		return;
	}

	GetData()->nDataLength = 0;

	for (;;)
	{
		// Read as many characters as we can, up to the delimiter:
		strm.get(m_pchData + GetData()->nDataLength, GetData()->nAllocLength - GetData()->nDataLength + 1, delim);

		// This is the new string length:
		GetData()->nDataLength += strlen_i(m_pchData + GetData()->nDataLength);

		// What stopped us?  An EOF?

#if _MSC_VER > 1200
		// In 2003, strm.get() sets an error if we are already at the delim.
		// Tosses us out of our binary search in files for hints and expansion of we don't reset.
		if (!strm && !strm.eof())
		{
			strm.clear(); // clear the error and continue...
		}
#endif

		if (!strm.good()
#if _MSC_VER > 1200
		    || strm.eof()
#endif
		)
		{
			break; // EOF encountered (or worse!)
		}

		// Nope.  Was it the delimiter?
		strm.get(ch);
		if (ch == delim)
			break; // Yup. We're done.  Don't put it back on the stream.
		else
			strm.putback(ch); // Nope, Put it back and keep going.

		// If we got here, the read must have stopped because the buffer
		// was going to overflow.  Resize and keep going.
		WtStringData* pOldData = GetData();
		int nOldLen = pOldData->nDataLength;
		int newLen = nOldLen + resizeInc;
		resizeInc *= 2;

		AllocBuffer(newLen, pOldData->data(), nOldLen);
		GetData()->nDataLength = nOldLen;

		Release(const_cast<WtStringData*>(pOldData));
	}
}

int WTString::rfind(LPCTSTR s, int p)
{
	return (int)string(c_str()).rfind(s, (uint)p);
}
int WTString::find_last_of(LPCTSTR s)
{
	return (int)string(c_str()).find_last_of(s);
}
uint WTString::hash() const
{
	extern UINT WTHashKey(std::string_view key);
	return WTHashKey(to_string_view());
	// 	UINT v = 0;
	// 	int n = length();
	// 	for(int i = n;i &&  ( (n-i) < 1  || m_pchData[i-1] != ':'); i--)
	// 		v = (v<<5) + v + m_pchData[i-1] + (m_pchData[i-1]<<5);
	// 	return v;
}
size_t WTString::xxhash() const
{
	return std::hash<WTString>()(*this);
}

/////////////////////////////////////////////////////////////////////////////
// static class data, special inlines
#if _MSC_VER > 1200
AFX_DATADEF const TCHAR afxChNil = '\0';
#endif

// For an empty string, m_pchData will point here
// (note: avoids special case of checking for NULL m_pchData)
// empty string data (and locked - Release/InterlockedDecrement special-cases afxDataNil)
static int rgInitData[] = {-1, 0, 0, 0};
static AFX_DATADEF WtStringData* afxDataNil = (WtStringData*)&rgInitData;
/*static*/ LPCTSTR __afxPchNil = (LPCTSTR)(((BYTE*)&rgInitData) + sizeof(WtStringData));
// special function to make afxEmptyString work even during initialization

//////////////////////////////////////////////////////////////////////////////
// Construction/Destruction

WTString::WTString(const OWL::string& str)
{
	Init();
	*this = str.c_str();
}

WTString::WTString()
{
	Init();
}

WTString::WTString(const WTString& stringSrc)
{
	_ASSERTE(stringSrc.GetData()->nRefs != 0);
#if NO_SHARED_REFS
	Init();
	AssignCopy(stringSrc.GetData()->nDataLength, stringSrc.m_pchData);
#else
	volatile WtStringData* pData = stringSrc.GetData();
	if (pData != afxDataNil)
	{
		::InterlockedIncrement(&pData->nRefs);
		// not really a race condition for m_pchData since this is the ctor
		::InterlockedExchangePointer((PVOID*)&m_pchData, const_cast<WtStringData*>(pData)->data());
	}
	else
	{
		Init();
	}
#endif
}

WTString::WTString(const CStringW& stringSrc)
{
	Init();
	*this = ::WideToMbcs(stringSrc, stringSrc.GetLength());
}

WTString::WTString(LPCWSTR lpsz)
{
	Init();
	if (lpsz && *lpsz)
		*this = ::WideToMbcs(lpsz, wcslen_i(lpsz));
}

WTString::WTString(wchar_t wch, int nRepeat /*= 1*/)
{
	Init();
	if (nRepeat >= 1)
	{
		if (wch >= 0x20 && wch <= 0x7e)
		{
			AllocBuffer(nRepeat, nullptr, 0);
#ifdef _UNICODE
			for (int i = 0; i < nLength; i++)
				m_pchData[i] = wch;
#else
			memset(m_pchData, wch, (uint)nRepeat);
#endif
		}
		else
		{
			CStringW wstr(wch, nRepeat);
			*this = ::WideToMbcs(wstr, wstr.GetLength());
		}
	}
}

namespace WTStringAllocator
{
constexpr uint32_t header_footer_size = sizeof(WtStringData) + sizeof(TCHAR); // null-terminator space

struct IWin32HeapWithSListPool
{
	virtual ~IWin32HeapWithSListPool()
	{
	}
	virtual void* create_item(uint32_t& size) = 0;
	virtual void destroy_item(void* item) = 0;
};
template <uint32_t item_size>
class CWin32HeapWithSListPool : public IWin32HeapWithSListPool
{
  public:
	CWin32HeapWithSListPool(uint32_t max_cached_items, size_t heapInitialSize, const char* heapName, DWORD heapFlags = 0, size_t heapMaxSize = 0)
	    : max_cached_items(max_cached_items), heap(std::make_shared<Win32Heap>(heapInitialSize, heapName, heapFlags, heapMaxSize
#ifdef VaWin32Heap_MemStats
	                                                                           ,
	                                                                           0
#endif
	                                                                           ))
	{
	}

	void* create_item(uint32_t& size) override
	{
#pragma warning(disable : 4127)
		if (item_size > 0)
		{
			assert(size <= item_size);
			size = item_size;
		}
#pragma warning(default : 4127)

		void* ret = nullptr;
		auto& free_items = _free_items;
		if (free_items.cnt > 0)
		{
			ret = free_items.items;
			assert(ret);
			free_items.items = *(void**)ret;
			--free_items.cnt;
#ifdef VaWTString_MemStats
			gWtstrAllocsRecycled += !!ret;
#endif
		}

		if (!ret)
			ret = heap->Alloc(size);
		return ret;
	}
	void destroy_item(void* item) override
	{
		assert(item);
		auto& free_items = _free_items;
		if (!free_items.disabled & (free_items.cnt < max_cached_items))
		{
			*(void**)item = free_items.items;
			free_items.items = item;
			free_items.cnt++;
			if (!free_items.heap) [[unlikely]]
				free_items.heap = heap;
		}
		else
			heap->Free(item);
	}

  protected:
	const uint32_t max_cached_items;
	std::shared_ptr<Win32Heap> heap;

	struct CFreeItems
	{
		~CFreeItems()
		{
			disabled = true;
			assert((!cnt && !items) || heap);
			while (items)
			{
				void* next = *(void**)items;
				heap->Free(items);
				items = next;
			}
		}

		std::shared_ptr<Win32Heap> heap = nullptr;
		void* items = nullptr;
		uint32_t cnt = 0;
		bool disabled = item_size == 0;
	};
	static inline thread_local CFreeItems _free_items;
};
// template <uint32_t I>
// thread_local CWin32HeapWithSListPool<I>::CFreeItems CWin32HeapWithSListPool<I>::free_items;

static IWin32HeapWithSListPool* heaps[258];
static std::once_flag heaps_init_flag;
static IWin32HeapWithSListPool& get_heap_with_init(uint32_t size);
static constinit IWin32HeapWithSListPool& (*get_heap)(uint32_t size) = get_heap_with_init;
static IWin32HeapWithSListPool& get_heap_quick(uint32_t size)
{
	return *heaps[std::clamp(size, 0u, 257u)];
}
static IWin32HeapWithSListPool& get_heap_with_init(uint32_t size)
{
	std::call_once(heaps_init_flag, [] {
#ifdef _WIN64
		IWin32HeapWithSListPool* h = new CWin32HeapWithSListPool<64>{2048, 1 * 1048576, "string_heap_64"};
		std::fill(&heaps[0], &heaps[65], h);
		h = new CWin32HeapWithSListPool<128>{1024, 1 * 1048576, "string_heap_128"};
		std::fill(&heaps[65], &heaps[129], h);
		h = new CWin32HeapWithSListPool<256>{1024, 2 * 1048576, "string_heap_256"};
		std::fill(&heaps[129], &heaps[257], h);
		heaps[257] = new CWin32HeapWithSListPool<0>{0, 2 * 1048576, "string_heap"};
#else
		IWin32HeapWithSListPool* h = new CWin32HeapWithSListPool<64>{256, 1 * 1048576, "string_heap_64"};
		std::fill(&heaps[0], &heaps[65], h);
		h = new CWin32HeapWithSListPool<128>{128, 1 * 1048576, "string_heap_128"};
		std::fill(&heaps[65], &heaps[129], h);
		h = new CWin32HeapWithSListPool<256>{128, 1 * 1048576, "string_heap_256"};
		std::fill(&heaps[129], &heaps[257], h);
		heaps[257] = new CWin32HeapWithSListPool<0>{0, 1 * 1048576, "string_heap"};
#endif
		::InterlockedCompareExchangePointer((void* volatile*)&get_heap, &get_heap_quick, &get_heap_with_init);
	});
	return get_heap_quick(size);
}

WtStringData* Alloc(uint32_t wanted_len/*, bool extend = true*/)
{
	uint32_t len = wanted_len + header_footer_size;

	// create_item will update len to rounded value!
	WtStringData* sd = (WtStringData *)get_heap(len).create_item(len);
	assert(sd);
	if(!sd)
		return nullptr;

	sd->nRefs = 1;
	sd->nDataLength = (int)wanted_len;
	sd->nAllocLength = int(len - header_footer_size);
	static_cast<LPTSTR>(sd->data())[wanted_len] = '\0';

#ifdef VaWTString_MemStats
	++gWtstrActiveAllocs;
	++gWtstrTotalAllocs;
	if (len <= 64)
	{
		++gWtstrActiveAllocs_64;
		++gWtstrTotalAllocs_64;
	}
	else if (len <= 128)
	{
		++gWtstrActiveAllocs_128;
		++gWtstrTotalAllocs_128;
	}
	else if (len <= 256)
	{
		++gWtstrActiveAllocs_256;
		++gWtstrTotalAllocs_256;
	}
	gWtstrCurAllocBytes += len;
	if (gWtstrCurAllocBytes > gWtstrMaxCumulativeBytes)
		gWtstrMaxCumulativeBytes = gWtstrCurAllocBytes.load();
#endif

	return sd;
}
void Free(WtStringData* pData)
{
	assert(pData);
	if(!pData)
		return;

	uint32_t total_len = pData->nAllocLength + header_footer_size;

#ifdef VaWTString_MemStats
	--gWtstrActiveAllocs;
	if (total_len <= 64)
		--gWtstrActiveAllocs_64;
	else if (total_len <= 128)
		--gWtstrActiveAllocs_128;
	else if (total_len <= 256)
		--gWtstrActiveAllocs_256;
	gWtstrCurAllocBytes -= total_len;
#endif

	get_heap(total_len).destroy_item(pData);
}

}
bool WTString::AllocBuffer(int nLen, LPCTSTR pStr, int strLen)
// always allocate one extra character for '\0' termination
// assumes [optimistically] that data length will equal allocation length
{
	_ASSERTE(nLen >= 0);
	_ASSERTE(nLen <= INT_MAX - 1); // max size (enough room for 1 extra)
	LPTSTR prevVal;
	WtStringData* pData;
	volatile WtStringData* pMyData = GetData();

	if (nLen == 0)
	{
		_ASSERTE(!strLen || (1 == strLen && pStr && pStr[0] == '\0'));
		// Init() has potential race condition - unrecovered, could cause leak (if wasn't afxDataNil to begin with)
		// this replaces Init()
		// potential race condition - dealt with below
		prevVal = (LPTSTR)::InterlockedExchangePointer((PVOID*)&m_pchData, WTafxEmptyString.m_pchData);
		pData = NULL;
	}
	else
	{
		pData = WTStringAllocator::Alloc((uint32_t)nLen);
		if (!pData)
		{
			_ASSERTE(!"allocation failure");
			vLog("ERROR: Wts::AllocBuffer fail %d", nLen);
			return false;
		}

		if (pStr && strLen)
		{
			// copy before assign to m_pchData
			_ASSERTE(strLen && strLen <= (nLen + 1));
			memcpy(static_cast<LPTSTR>(pData->data()), pStr, strLen * sizeof(TCHAR));
			if (strLen < nLen)
				static_cast<LPTSTR>(pData->data())[strLen] = '\0';
		}

		// potential race condition - dealt with below
		prevVal = (LPTSTR)::InterlockedExchangePointer((PVOID*)&m_pchData, static_cast<LPTSTR>(pData->data()));
	}

	WtStringData* potentiallyOverwrittenData = ((WtStringData*)prevVal) - 1;
	if (potentiallyOverwrittenData != afxDataNil && potentiallyOverwrittenData != pMyData) [[unlikely]]
	{
		// race condition - we won and overwrote someone else.
		if (potentiallyOverwrittenData->nAllocLength > nLen)
		{
			// they created a large buf, put them back
			// potential race condition
			prevVal = (LPTSTR)::InterlockedExchangePointer((PVOID*)&m_pchData,
			                                               static_cast<LPTSTR>(potentiallyOverwrittenData->data()));
			_ASSERTE(prevVal == pData->data()); // if this fires, we leaked a different one (whatever prevVal came from)
#ifdef VaWTString_MemStats
			++gWtstrClashes;
			--gWtstrActiveAllocs;
			uint32_t kSz = sizeof(WtStringData) + (pData->nAllocLength + 1) * sizeof(TCHAR);
			gWtstrCurAllocBytes -= kSz;
#endif
			WTStringAllocator::Free(pData);
		}
		else
		{
			// our buf was larger, we keep the win
			Release(potentiallyOverwrittenData);
		}
	}

	return true;
}

void PASCAL WTString::Release(WtStringData* pData)
{
	if (pData != afxDataNil)
	{
		_ASSERTE(pData->nRefs != 0 && pData->nRefs != 0xdddddddd);
		const long rc = ::InterlockedDecrement(&pData->nRefs);
		if (rc == 0)
			WTStringAllocator::Free(pData);
		else
		{
			_ASSERTE(rc > 0 && rc < 500000);
		}
	}
}

void WTString::Empty()
{
	volatile WtStringData* pOldData = GetData();
	if (pOldData->nDataLength == 0)
		return;
	_ASSERTE(pOldData != afxDataNil); // can't be empty string if nDataLength != 0
	Init();                           // potential race condition - potential leak if Empty() called on a global concurrently with AllocBuffer (as
	                                  // an example)
	Release(const_cast<WtStringData*>(pOldData));
	_ASSERTE(GetData() == afxDataNil);
	_ASSERTE(GetData()->nDataLength == 0);
	_ASSERTE(GetData()->nAllocLength == 0);
}

void WTString::CopyBeforeWrite()
{
	volatile WtStringData* pOldData = GetData();
	if (pOldData->nRefs > 1)
	{
#ifdef VaWTString_MemStats
		++gWtstrCopies;
#endif
		if (AllocBuffer(pOldData->nDataLength, const_cast<WtStringData*>(pOldData)->data(), pOldData->nDataLength + 1))
			Release(const_cast<WtStringData*>(pOldData));
	}
	_ASSERTE(GetData()->nRefs <= 1);
}

bool WTString::AllocBeforeWrite(int nLen)
{
	volatile WtStringData* pOldData = GetData();
	if (pOldData->nRefs > 1 || nLen > pOldData->nAllocLength)
	{
#ifdef VaWTString_MemStats
		if(pOldData->nRefs > 1)
			++gWtstrCopies;
		else if (pOldData != afxDataNil)
			++gWtstrReallocs;
#endif

		if (AllocBuffer(nLen, nullptr, 0))
			Release(const_cast<WtStringData*>(pOldData));
		else
			return false;
	}
	_ASSERTE(GetData()->nRefs <= 1);
	return true;
}

WTString::~WTString()
//  free any attached data
{
	WtStringData* pData = GetData();
	Release(pData);
}

//////////////////////////////////////////////////////////////////////////////
// Helpers for the rest of the implementation

void WTString::AllocCopy(WTString& dest, int nCopyLen, int nCopyIndex, int nExtraLen) const
{
	// will clone the data attached to this string
	// allocating 'nExtraLen' characters
	// Places results in uninitialized string 'dest'
	// Will copy the part or all of original data to start of new string

	int nNewLen = nCopyLen + nExtraLen;
	if (nNewLen == 0)
	{
		dest.Init(); // potential race condition - but is protected helper that is only used on unshared stack objects
	}
	else
	{
		// unshared, only used on the stack
		_ASSERTE(dest.m_pchData == afxDataNil->data());
		dest.AllocBuffer(nNewLen, m_pchData + nCopyIndex, nCopyLen);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Diagnostic support

#ifdef _DEBUG
CDumpContext& AFXAPI operator<<(CDumpContext& dc, const WTString& string)
{
	dc << string.m_pchData;
	return dc;
}
#endif //_DEBUG

//////////////////////////////////////////////////////////////////////////////
// Assignment operators
//  All assign a new value to the string
//      (a) first see if the buffer is big enough
//      (b) if enough room, copy on top of old buffer, set size and type
//      (c) otherwise free old string data, and create a new one
//
//  All routines return the new string (but as a 'const WTString&' so that
//      assigning it again will cause a copy, eg: s1 = s2 = "hi there".
//

void WTString::AssignCopy(int nSrcLen, LPCTSTR lpszSrcData)
{
	AllocBeforeWrite(nSrcLen);
	if(!IsStaticEmpty()) [[likely]]
	{
		memmove(m_pchData, lpszSrcData, nSrcLen * sizeof(TCHAR));
		GetData()->nDataLength = nSrcLen;
		m_pchData[nSrcLen] = '\0';
		_ASSERTE(GetData()->nDataLength == strlen_i(c_str()));
	}
}

WTString& WTString::operator=(const WTString& stringSrc)
{
	if (m_pchData != stringSrc.m_pchData)
	{
#if NO_SHARED_REFS
		AssignCopy(stringSrc.GetData()->nDataLength, stringSrc.m_pchData);
#else
		volatile WtStringData* pMyData = GetData();
		volatile WtStringData* pSrcData = stringSrc.GetData();
		if (pSrcData == afxDataNil)
		{
			Init();
		}
		else
		{
			// can just copy references around
			::InterlockedIncrement(&pSrcData->nRefs);
			// potential race condition - dealt with below
			LPTSTR prevVal =
			    (LPTSTR)::InterlockedExchangePointer((PVOID*)&m_pchData, const_cast<WtStringData*>(pSrcData)->data());
			_ASSERTE(m_pchData == const_cast<WtStringData*>(pSrcData)->data());
			if (prevVal != const_cast<WtStringData*>(pMyData)->data())
			{
				// race - we won and overwrote someone else.  Decrement their refcnt to prevent leak.
				WtStringData* overwrittenData = ((WtStringData*)prevVal) - 1;
				Release(overwrittenData);
			}
		}
		Release(const_cast<WtStringData*>(pMyData));
#endif
	}
	return *this;
}

WTString& WTString::operator=(const char* lpsz)
{
	_ASSERTE(lpsz == NULL || AfxIsValidString(lpsz, FALSE));
	if (!is_my_ptr(lpsz)) [[likely]]
		AssignCopy(SafeStrlen(lpsz), lpsz);
	else
		MidInPlace(int(lpsz - c_str()));
	return *this;
}
WTString& WTString::operator=(const std::string_view sv)
{
	if (!is_my_ptr(sv.data())) [[likely]]
		AssignCopy((int)sv.size(), sv.data());
	else
		MidInPlace(sv);
	return *this;
}

/////////////////////////////////////////////////////////////////////////////
// Special conversion assignment

#ifdef _UNICODE
// WTString& WTString::operator=(LPCSTR lpsz)
// {
// 	int nSrcLen = lpsz != NULL ? lstrlenA(lpsz) : 0;
// 	AllocBeforeWrite(nSrcLen);
// 	_mbstowcsz(m_pchData, lpsz, nSrcLen+1);
// 	ReleaseBuffer();
// 	return *this;
// }
// #else //!_UNICODE
// WTString& WTString::operator=(LPCWSTR lpsz)
// {
// 	int nSrcLen = lpsz != NULL ? wcslen(lpsz) : 0;
// 	AllocBeforeWrite(nSrcLen*2);
// 	_wcstombsz(m_pchData, lpsz, (nSrcLen*2)+1);
// 	ReleaseBuffer();
// 	return *this;
// }
#endif //!_UNICODE

//////////////////////////////////////////////////////////////////////////////
// concatenation

// NOTE: "operator+" is done as friend functions for simplicity
//      There are three variants:
//          WTString + WTString
// and for ? = TCHAR, LPCTSTR
//          WTString + ?
//          ? + WTString

void WTString::ConcatCopyUnchecked(int nSrc1Len, LPCTSTR lpszSrc1Data, int nSrc2Len, LPCTSTR lpszSrc2Data)
{
	// -- master concatenation routine
	// Concatenate two sources

	int nNewLen = nSrc1Len + nSrc2Len;
	if (nNewLen != 0)
	{
		// potential leak if not wrapped with a GetData() and subsequent Release().
		// ConcatCopyUnchecked should only be called when wrapped as above or via
		// ConcatCopy (has precondition assert)
		AllocBuffer((nNewLen * 2) + 50, lpszSrc1Data, nSrc1Len);
		GetData()->nDataLength = nNewLen;
		// even if lpszSrc2Data points to the current buffer, it's ok because Release is done after this call
		memcpy(m_pchData + nSrc1Len, lpszSrc2Data, nSrc2Len * sizeof(TCHAR));
		m_pchData[nNewLen] = '\0';
	}
	_ASSERTE(GetData()->nDataLength == strlen_i(c_str()));
}

void WTString::ConcatCopy(int nSrc1Len, LPCTSTR lpszSrc1Data, int nSrc2Len, LPCTSTR lpszSrc2Data)
{
	// Concatenate two sources
	// -- assume that 'this' is a new WTString object

	int nNewLen = nSrc1Len + nSrc2Len;
	if (nNewLen != 0)
	{
		_ASSERTE(GetData() == afxDataNil); // otherwise, AllocBuffer will cause leak of GetData()
		ConcatCopyUnchecked(nSrc1Len, lpszSrc1Data, nSrc2Len, lpszSrc2Data);
	}
	_ASSERTE(GetData()->nDataLength == strlen_i(c_str()));
}

WTString operator+(const WTString& str1, std::string_view sv2)
{
	WTString s;
	s.ConcatCopy(str1.GetData()->nDataLength, str1.m_pchData, (int)sv2.length(), sv2.data());
	return s;
}
WTString operator+(const WTString& str1, const char* str2)
{
	int str2len = WTString::SafeStrlen(str2);
	if(!str2len)
		return str1;

	WTString s;
	s.ConcatCopy(str1.GetData()->nDataLength, str1.m_pchData, str2len, str2);
	return s;
}

WTString operator+(const char* str1, WTString&& str2)
{
	int str1len = WTString::SafeStrlen(str1);
	if (!str1len)
		return str2;

	str2.insert_new(0, std::string_view(str1, str1 + str1len));
	return std::move(str2);
}
WTString operator+(const char* str1, const WTString& str2)
{
	_ASSERTE(str1 == NULL || AfxIsValidString(str1, FALSE));
	int lpsz_len = WTString::SafeStrlen(str1);
	if(!lpsz_len)
		return str2;

	WTString s;
	s.ConcatCopy(lpsz_len, str1, str2.GetData()->nDataLength, str2.m_pchData);
	return s;
}

WTString operator+(char ch, WTString&& string)
{
	string.insert_new(0, std::string_view(&ch, &ch + 1));
	return std::move(string);
}
WTString operator+(char ch, const WTString& string)
{
	if (!ch)
		return string;

	WTString s;
	s.ConcatCopy(1, &ch, string.GetData()->nDataLength, string.m_pchData);
	return s;
}

//////////////////////////////////////////////////////////////////////////////
// concatenate in place

#pragma warning(push)
#pragma warning(disable : 5214)
void WTString::ConcatInPlace(int nSrcLen, LPCTSTR lpszSrcData)
{
	//  -- the main routine for += operators

	// concatenating an empty string is a no-op!
	if (nSrcLen == 0)
		return;

#if defined(VA_CPPUNIT)
	extern bool gEnableAllAsserts;
	if (gEnableAllAsserts)
#endif
		_ASSERTE(GetData()->nDataLength == strlen_i(c_str()) &&
		         "stop now and fix the caller of WTString::ConcatInPlace - the caller is broken");

	// 	if(GetData()->nDataLength != strlen_i(*this)){
	// 		// string contains a null terminator
	// 		// we are appending stuff past the NULL terminator
	// 		_ASSERTE(!"WTString::ConcatInPlace strlen assert");
	// 		AssignCopy(SafeStrlen(*this), *this);
	// 	}

	// if the buffer is too small, or we have a width mis-match, just
	//   allocate a new buffer (slow but sure)
	volatile WtStringData* pOldData = GetData();
	if (pOldData->nRefs > 1 || pOldData->nDataLength + nSrcLen > pOldData->nAllocLength)
	{
		// we have to grow the buffer, use the ConcatCopy routine
		// use ConcatCopyUnchecked since we wrapped the call with GetData()/Release()
		ConcatCopyUnchecked(pOldData->nDataLength, const_cast<WtStringData*>(pOldData)->data(), nSrcLen, lpszSrcData);
		_ASSERTE(pOldData != NULL);
		Release(const_cast<WtStringData*>(pOldData));
	}
	else
	{
		// fast concatenation when buffer big enough
		if (1 == nSrcLen)
		{
#if defined(VA_CPPUNIT)
			if (gEnableAllAsserts)
#endif
				_ASSERTE(lpszSrcData && *lpszSrcData);

			m_pchData[pOldData->nDataLength] = *lpszSrcData;
			pOldData->nDataLength++;
		}
		else
		{
			memcpy(m_pchData + pOldData->nDataLength, lpszSrcData, nSrcLen * sizeof(TCHAR));
			pOldData->nDataLength += nSrcLen;
		}

		_ASSERTE(pOldData->nDataLength <= pOldData->nAllocLength);
		m_pchData[pOldData->nDataLength] = '\0';
	}
}
#pragma warning(pop)

WTString& WTString::operator+=(const char* lpsz)
{
	_ASSERTE(lpsz == NULL || AfxIsValidString(lpsz, FALSE));
	assert(!is_my_ptr(lpsz));
	if (!is_my_ptr(lpsz)) [[likely]]
		ConcatInPlace(SafeStrlen(lpsz), lpsz);
	else
		*this += {std::string_view(lpsz)}; // bad performance, should never happen, but be correct
	return *this;
}

WTString& WTString::operator+=(std::string_view sv)
{
	assert(!is_my_ptr(sv.data()));
	if (!is_my_ptr(sv.data())) [[likely]]
		ConcatInPlace((int)sv.length(), sv.data());
	else
		*this += {sv}; // bad performance, should never happen, but be correct
	return *this;
}
template<bool detect_inplace>
static inline std::pair<size_t, bool> get_initializer_list_size(const WTString *s, const std::initializer_list<std::string_view> &l)
{
	assert(!detect_inplace || s);
	size_t size = 0;
	bool has_inplace_view = false;
	for(const auto &sv : l)
	{
		size += sv.length();
		if constexpr(detect_inplace)
			has_inplace_view |= s->is_my_ptr(sv.data());
	}
	return {size, has_inplace_view};
}
void WTString::do_il_concat(const std::initializer_list<std::string_view> &l, size_t added_size, bool has_inplace_view)
{
	if (!added_size)
		return;

	uint32_t new_size = length() + (uint32_t)added_size;
	// if reallocation will happen and we have a component pointing within the existing string, we need a fresh WTString
	bool use_new_str = has_inplace_view && ((int)new_size > GetData()->nAllocLength);
	WTString new_str;
	WTString& s = *(use_new_str ? &new_str : this);

	s.reserve(new_size);
	for (const auto& sv : l)
		s += sv;

	if (use_new_str)
		*this = std::move(s);
}
WTString& WTString::operator+=(std::initializer_list<std::string_view> l)
{
	auto [added_size, has_inplace_view] = get_initializer_list_size<true>(this, l);

	do_il_concat(l, added_size, has_inplace_view);
	return *this;
}
WTString& WTString::operator=(std::initializer_list<std::string_view> l)
{
	auto [added_size, has_inplace_view] = get_initializer_list_size<true>(this, l);
	if (has_inplace_view) [[unlikely]]
	{
		assert(!"do not use this if you don't have to");
		*this = WTString(l);
	}
	else
	{
		Clear();
		do_il_concat(l, added_size, has_inplace_view);
	}
	return *this;
}

bool operator==(std::string_view sv, std::initializer_list<std::string_view> svs)
{
	auto [il_size, _] = get_initializer_list_size<false>(nullptr, svs);
	if(sv.length() != il_size)
		return false;

	size_t i = 0;
	for(const auto &sv2 : svs)
	{
		if(sv2 != sv.substr(i, sv2.length()))
			return false;
		i += sv2.length();
	}
	return true;
}

///////////////////////////////////////////////////////
// token items
void WTString::ReplaceAt(int pos, int len, LPCSTR str)
{
	assign(Mid(0, pos) + str + Mid(pos + len));
}
void WTString::ReplaceAt_new(size_t pos, size_t len, std::string_view sv)
{
	assert((pos + len) <= GetULength());
	if((pos + len) > GetULength())
		return;

	if(sv.size() < len)
		remove(pos + sv.size(), len - sv.size());
	else if(sv.size() > len)
		insert_new(pos + len, sv.substr(len));
	memcpy(GetBuffer(0) + pos, sv.data(), std::min(len, sv.size()));
	ReleaseBuffer(GetLength());
}

int WTString::ReplaceAll(LPCSTR s1, std::string_view s2, BOOL wholeword)
{
	std::string_view s1_sv(s1);
	if(s1_sv == s2)
		return 0;

	int changed = 0;
	LPCSTR buf = c_str();
	LPCSTR fp = wholeword ? strstrWholeWord(buf, s1) : strstr(buf, s1);
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		while (fp && s1 && *s1)
		{
			changed++;
			{
				size_t offset = size_t(fp - buf);
#ifdef _DEBUG
				WTString teststr = *this;
				teststr.ReplaceAt((int)offset, (int)s1_sv.length(), std::string(s2).c_str());
#endif
				ReplaceAt_new(offset, s1_sv.length(), s2);
				assert(teststr == *this);
				buf = c_str(); // replace at may reallocate buf
				fp = buf + offset +
				     s2.size(); // set to end of inserted string, don't replace, what was already replaced
			}
			fp = wholeword ? strstrWholeWord(fp, s1) : strstr(fp, s1);
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("WTS:");
		_ASSERTE(!"WTString::ReplaceAll");
		if (!Psettings->m_catchAll)
		{
			_ASSERTE(!"Fix the bad code that caused this exception in WTString::ReplaceAll");
		}
	}
#endif // !SEAN
	return changed;
}

void WTString::ReplaceAll(char from, char to)
{
	int l = GetLength();
	int i;
	for (i = 0; i < l; i++)
	{
		if (GetAt(i) == from)
		{
			SetAt(i, to);
			assert(GetData()->nRefs == 1);
			break;
		}
	}
	// after the first SetAt, scope will have a refcnt of 1, so further SetAts don't need to verify that
	for (++i; i < l; i++)
	{
		if (GetAt(i) == from)
			SetAt_nocopy(i, to);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Replace using regular expressions

template <typename charT>
static int ReplaceAllRegex(const charT* pattern, bool iCase, const charT* input, WTString& out,
                           std::function<bool(int, ATL::CStringT<charT, StrTraitMFC<charT>>&)> fmtFnc)
{
	int num_changes = 0;

	if (!fmtFnc)
		return 0;

	try
	{
		typedef ATL::CStringT<charT, StrTraitMFC<charT>> T_CString;
		typedef std::match_results<const charT*> tmatch;
		typedef std::basic_regex<charT> tregex;

		std::regex::flag_type flags = std::regex::collate | std::regex::nosubs | std::wregex::ECMAScript;

		if (iCase)
			flags |= std::regex::icase;

		tmatch m;
		tregex rgx(pattern, flags);

		const charT* str_in = input;

		bool do_continue = true;

		while (std::regex_search(str_in, m, rgx))
		{
			if (m.prefix().length())
				out += m.prefix().str().c_str();

			T_CString match_rslt = m.str().c_str();
			do_continue = fmtFnc((int)m.position(), match_rslt);
			out += match_rslt;
			num_changes++;

			if (!do_continue)
				break;

			str_in += m.position() + m.length();
		}

		if (num_changes == 0)
			out = input;
		else if (m.suffix().length())
			out += m.suffix().str().c_str();
	}
	catch (const std::exception& e)
	{
		VALOGEXCEPTION("WTS:");
		VALogError(e.what() ? e.what() : "Unknown error.", __LINE__, TRUE);
		_ASSERTE(!"WTString::ReplaceAllRE");
	}
	catch (...)
	{
		VALOGEXCEPTION("WTS:");
		_ASSERTE(!"WTString::ReplaceAllRE");
	}

	return num_changes;
}

int WTString::ReplaceAllRE(LPCWSTR pattern, bool iCase, std::function<bool(int, CStringW&)> fmtFnc)
{
	CStringW input = Wide();
	Empty();
	return ReplaceAllRegex<WCHAR>(pattern, iCase, input, *this, fmtFnc);
}

int WTString::ReplaceAllRE(LPCWSTR pattern, bool iCase, const CStringW& replacement)
{
	CStringW input = Wide();
	Empty();
	try
	{
		std::wregex::flag_type flags = std::wregex::ECMAScript | std::wregex::collate;
		if (iCase)
			flags |= std::regex::icase;

		std::wregex rgx(pattern, flags);

		{
			std::wstring str = std::regex_replace((LPCWSTR)input, rgx, (LPCWSTR)replacement);
			assign(WTString(str.c_str()));
		}

		return GetLength();
	}
	catch (const std::exception& e)
	{
		VALOGEXCEPTION("WTS:");
		VALogError(e.what() ? e.what() : "Unknown error.", __LINE__, TRUE);
		_ASSERTE(!"WTString::ReplaceAllRE");
	}
	catch (...)
	{
		VALOGEXCEPTION("WTS:");
		_ASSERTE(!"WTString::ReplaceAllRE");
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Advanced direct buffer access

LPTSTR WTString::GetBuffer(int nMinBufLength)
{
	_ASSERTE(nMinBufLength >= 0);
	volatile WtStringData* pOldData = GetData();
	if (0 == nMinBufLength && pOldData->nRefs == 0xffffffff)
	{
		_ASSERTE(m_pchData == afxDataNil->data());
		// force a new allocation
		nMinBufLength = 1000;
	}

	if (pOldData->nRefs > 1 || nMinBufLength > pOldData->nAllocLength)
	{
		// we have to grow the buffer
		int nOldLen = pOldData->nDataLength; // AllocBuffer will tromp it
		if (nMinBufLength < nOldLen)
			nMinBufLength = nOldLen;
		AllocBuffer(nMinBufLength, const_cast<WtStringData*>(pOldData)->data(), nOldLen + 1);
		GetData()->nDataLength = nOldLen;
		Release(const_cast<WtStringData*>(pOldData));
	}
	_ASSERTE(GetData()->nRefs <= 1);

	// return a pointer to the character storage for this string
#ifdef _DEBUG
	if (m_pchData == NULL || m_pchData == afxDataNil->data())
	{
		_ASSERTE(m_pchData != NULL && m_pchData != afxDataNil->data());
	}
#endif // _DEBUG

	return m_pchData;
}

void WTString::ReleaseBuffer(int nNewLength)
{
#ifdef _DEBUG
	if (m_pchData == NULL || m_pchData == afxDataNil->data())
	{
		_ASSERTE(m_pchData != NULL && m_pchData != afxDataNil->data());
	}
#endif                 // _DEBUG
	CopyBeforeWrite(); // just in case GetBuffer was not called

	if (nNewLength == -1)
		nNewLength = lstrlen(m_pchData); // zero terminated

	_ASSERTE(nNewLength <= GetData()->nAllocLength);
	GetData()->nDataLength = nNewLength;
	m_pchData[nNewLength] = '\0';
}

/*void WTString::FreeExtra()
{
	volatile WtStringData* pOldData = GetData();
	_ASSERTE(pOldData->nDataLength <= pOldData->nAllocLength);
	if (pOldData->nDataLength != pOldData->nAllocLength)
	{
		AllocBuffer(pOldData->nDataLength, const_cast<WtStringData*>(pOldData)->data(), pOldData->nDataLength);
		_ASSERTE(m_pchData[GetData()->nDataLength] == '\0');
		Release(const_cast<WtStringData*>(pOldData));
	}
	_ASSERTE(GetData() != NULL);
}*/

///////////////////////////////////////////////////////////////////////////////
// Commonly used routines (rarely used routines in STREX.CPP)

int WTString::Find(TCHAR ch, int startPos) const
{
	if(startPos < 0)
		startPos = 0;
	if(startPos >= GetLength())
		return -1;

	// find first single character
	LPTSTR lpsz = (LPTSTR)_tcschr(m_pchData + startPos, (_TUCHAR)ch);

	// return -1 if not found and index otherwise
	return (lpsz == NULL) ? -1 : (int)(lpsz - m_pchData);
}

int WTString::FindOneOf(LPCTSTR lpszCharSet) const
{
	_ASSERTE(AfxIsValidString(lpszCharSet, FALSE));
	LPTSTR lpsz = _tcspbrk(m_pchData, lpszCharSet);
	return (lpsz == NULL) ? -1 : (int)(lpsz - m_pchData);
}

void WTString::MakeUpper()
{
	CopyBeforeWrite();
	if (Psettings && Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, "C"); // use 'C' sort conventions
	_tcsupr(m_pchData);
	if (Psettings && Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, ""); // restore
}

void WTString::MakeLower()
{
	CopyBeforeWrite();
	if (Psettings && Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, "C"); // use 'C' sort conventions
	_tcslwr(m_pchData);
	if (Psettings && Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, ""); // restore
}

void WTString::MakeReverse()
{
	CopyBeforeWrite();
	if (Psettings && Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, "C"); // use 'C' sort conventions
	_tcsrev(m_pchData);
	if (Psettings && Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, ""); // restore
}

void WTString::SetAt(int nIndex, TCHAR ch)
{
	_ASSERTE(nIndex >= 0);
	_ASSERTE(nIndex < GetData()->nDataLength);

	CopyBeforeWrite();
	m_pchData[nIndex] = ch;
}
void WTString::SetAt_nocopy(int nIndex, TCHAR ch)
{
	// be sure to call that on a WTString that exists as a single instance!!
	_ASSERTE(nIndex >= 0);
	_ASSERTE(nIndex < GetData()->nDataLength);
	_ASSERTE(GetData()->nRefs == 1);

	m_pchData[nIndex] = ch;
}

#ifndef _UNICODE
void WTString::AnsiToOem()
{
	CopyBeforeWrite();
	::AnsiToOem(m_pchData, m_pchData);
}
void WTString::OemToAnsi()
{
	CopyBeforeWrite();
	::OemToAnsi(m_pchData, m_pchData);
}
#endif

CStringW WTString::Wide() const
{
	return ::MbcsToWide(m_pchData, GetLength());
}

////////////////////////////////
// More sophisticated construction

WTString::WTString(char ch, int nLength)
{
	_ASSERTE(!_istlead((uchar)ch)); // can't create a lead byte string
	Init();
	if (nLength >= 1)
	{
		AllocBuffer(nLength, nullptr, 0);
#ifdef _UNICODE
		for (int i = 0; i < nLength; i++)
			m_pchData[i] = ch;
#else
		memset(m_pchData, ch, (uint)nLength);
#endif
	}
}

WTString::WTString(LPCTSTR lpch, int nLength)
{
	Init();
	if (nLength != 0)
	{
		if (nLength < 0)
		{
			_ASSERTE(!"negative str len passed to WTString ctor -- try to fix caller");
			int len;
			for (len = 0; len < 1024 && lpch[len]; len++)
				; // len is bad, read to null terminator
			nLength = len;
		}
		else if (nLength != 0)
		{
#ifdef _DEBUG
			int len;
			for (len = 0; len < nLength && lpch[len]; len++)
				; // make sure len is correct size
			// if this assert fires, fix the caller because the len safety measure
			// has been removed (see WTString.cpp#3) since it prevents the efficiency
			// that passing in nLength is supposed to be used for
			_ASSERTE(nLength == len && "Stop and Fix this because it is broken in release builds!");
			nLength = len; // in case nLength is greater than strlen.
#endif                     // _DEBUG
		}

		_ASSERTE(AfxIsValidAddress(lpch, (uint)nLength, FALSE));
		AllocBuffer(nLength, lpch, nLength);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Assignment operators

WTString& WTString::operator=(TCHAR ch)
{
	if (!ch) [[unlikely]]
		Clear();
	else
	{
		_ASSERTE(!_istlead((uchar)ch)); // can't set single lead byte
		AssignCopy(1, &ch);
	}
	return *this;
}

//////////////////////////////////////////////////////////////////////////////
// Very simple sub-string extraction

WTString WTString::Mid(int nFirst) const
{
	return Mid(nFirst, GetData()->nDataLength - nFirst);
}

WTString WTString::Mid(int nFirst, int nCount) const
{
	// out-of-bounds requests return sensible things
	if (nFirst < 0)
		nFirst = 0;
	if (nCount < 0)
		nCount = 0;

	if (nFirst + nCount > GetData()->nDataLength)
		nCount = GetData()->nDataLength - nFirst;
	if (nFirst > GetData()->nDataLength)
		nCount = 0;

	WTString dest;
	AllocCopy(dest, nCount, nFirst, 0);
	return dest;
}

void WTString::MidInPlace(int nFirst)
{
	assert(nFirst >= 0 && nFirst <= GetData()->nDataLength);
	nFirst = std::clamp(nFirst, 0, GetData()->nDataLength);

	MidInPlace(nFirst, GetData()->nDataLength - nFirst);
}
void WTString::MidInPlace(int nFirst, int nCount)
{
	WtStringData* pData = GetData();
	if (pData->nRefs > 1)
	{
		*this = Mid(nFirst, nCount);
		return;
	}

	assert(nFirst <= pData->nDataLength);
	assert((nFirst + nCount) <= pData->nDataLength);
	nFirst = std::clamp(nFirst, 0, pData->nDataLength);
	nCount = std::clamp(nCount, 0, pData->nDataLength - nFirst);

	if((nFirst > 0) & (nCount > 0))
		memmove(pData->data(), &pData->data()[nFirst], (uint32_t)nCount);
	pData->nDataLength = nCount;
	pData->data()[nCount] = 0;
}

void WTString::MidInPlace(std::string_view sv)
{
	// fast mid if sv is within WTstring
	if(sv.empty())
		Clear();
	else if((sv.data() >= c_str()) && ((sv.data() + sv.length()) <= (c_str() + length())))
		MidInPlace(int(sv.data() - c_str()), (int)sv.length());
	else
		AssignCopy((int)sv.size(), sv.data());
}

WTString WTString::Right(int nCount) const
{
	if (nCount < 0)
		nCount = 0;
	else if (nCount > GetData()->nDataLength)
		nCount = GetData()->nDataLength;

	WTString dest;
	AllocCopy(dest, nCount, GetData()->nDataLength - nCount, 0);
	return dest;
}

WTString WTString::Left(int nCount) const
{
	if (nCount < 0)
		nCount = 0;
	else if (nCount > GetData()->nDataLength)
		nCount = GetData()->nDataLength;

	WTString dest;
	AllocCopy(dest, nCount, 0, 0);
	return dest;
}

void WTString::LeftInPlace(int nCount)
{
	if (nCount >= GetLength())
		return;
	if (nCount <= 0)
		Clear();
	else
	{
		WtStringData* pData = GetData();
		if (pData->nRefs > 1)
			*this = Left(nCount);
		else
		{
			pData->nDataLength = nCount;
			pData->data()[nCount] = 0;
		}
	}
}

// strspn equivalent
WTString WTString::SpanIncluding(LPCTSTR lpszCharSet) const
{
	_ASSERTE(AfxIsValidString(lpszCharSet, FALSE));
	return Left(_tcsspn(m_pchData, lpszCharSet));
}

// strcspn equivalent
WTString WTString::SpanExcluding(LPCTSTR lpszCharSet) const
{
	_ASSERTE(AfxIsValidString(lpszCharSet, FALSE));
	return Left(_tcscspn(m_pchData, lpszCharSet));
}

//////////////////////////////////////////////////////////////////////////////
// Finding

int WTString::ReverseFind(TCHAR ch) const
{
	// find last single character
	LPTSTR lpsz = _tcsrchr(m_pchData, (_TUCHAR)ch);

	// return -1 if not found, distance from beginning otherwise
	return (lpsz == NULL) ? -1 : (int)(lpsz - m_pchData);
}

int WTString::FindRE(LPCTSTR pattern, int startPos /*= 0*/) const
{
	_ASSERTE(AfxIsValidString(pattern, FALSE));

	typedef std::match_results<LPCTSTR> tcmatch;
	typedef std::basic_regex<TCHAR> tregex;

	try
	{
		tregex re(pattern, std::regex::collate | std::regex::nosubs | std::regex::ECMAScript);
		tcmatch m;
		if (std::regex_search(&m_pchData[startPos], m, re))
			return int(startPos + m.position());
	}
	catch (const std::exception& e)
	{
		(void)e;
		//		LPCSTR what = e.what();
		_ASSERTE(!"Exception in WTString::FindRE");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in WTString::FindRE");
	}

	return -1;
}

bool WTString::MatchRE(LPCTSTR pattern) const
{
	_ASSERTE(AfxIsValidString(pattern, FALSE));

	typedef std::match_results<LPCTSTR> tcmatch;
	typedef std::basic_regex<TCHAR> tregex;

	try
	{
		tregex re(pattern, std::regex::collate | std::regex::nosubs | std::regex::ECMAScript);
		return std::regex_match(m_pchData, re);
	}
	catch (const std::exception& e)
	{
		(void)e;
		//		LPCSTR what = e.what();
		_ASSERTE(!"Exception in WTString::MatchRE");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in WTString::MatchRE");
	}

	return false;
}

bool WTString::MatchRENoCase(LPCTSTR pattern) const
{
	_ASSERTE(AfxIsValidString(pattern, FALSE));

	typedef std::match_results<LPCTSTR> tcmatch;
	typedef std::basic_regex<TCHAR> tregex;

	try
	{
		tregex re(pattern, std::regex::collate | std::regex::icase | std::regex::nosubs | std::regex::ECMAScript);
		return std::regex_match(m_pchData, re);
	}
	catch (const std::exception& e)
	{
		(void)e;
		//		LPCSTR what = e.what();
		_ASSERTE(!"Exception in WTString::MatchRENoCase");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in WTString::MatchRENoCase");
	}

	return false;
}

int WTString::FindRENoCase(LPCTSTR pattern, int startPos /*= 0*/) const
{
	_ASSERTE(AfxIsValidString(pattern, FALSE));

	typedef std::match_results<LPCTSTR> tcmatch;
	typedef std::basic_regex<TCHAR> tregex;

	try
	{
		tregex re(pattern, std::regex::collate | std::regex::icase | std::regex::nosubs | std::regex::ECMAScript);
		tcmatch m;
		if (std::regex_search(&m_pchData[startPos], m, re))
			return int(startPos + m.position());
	}
	catch (const std::exception& e)
	{
		(void)e;
		//		LPCSTR what = e.what();
		_ASSERTE(!"Exception in WTString::FindRENoCase");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in WTString::FindRENoCase");
	}

	return -1;
}

// find a sub-string (like strstr)
int WTString::Find(LPCTSTR lpszSub, int startPos /*= 0*/) const
{
	_ASSERTE(AfxIsValidString(lpszSub, FALSE));

	// find first matching substring
	LPTSTR lpsz = _tcsstr(&m_pchData[startPos], lpszSub);

	// return -1 for not found, distance from beginning otherwise
	return (lpsz == NULL) ? -1 : (int)(lpsz - m_pchData);
}

// case-insensitive sub-string find
int WTString::FindNoCase(LPCTSTR lpszSub, int startPos /*= 0*/) const
{
	_ASSERTE(AfxIsValidString(lpszSub, FALSE));

	// change case
	WTString substrToFind(lpszSub);
	substrToFind.MakeLower();

	WTString lowerStr(c_str());
	lowerStr.MakeLower();
	return lowerStr.Find(substrToFind.c_str(), startPos);
}

int WTString::Find2(const char* substr, char char_after_substr) const
{
	_ASSERTE(AfxIsValidString(substr));

	auto substr_len = strlen(substr);
	for (const char* s = c_str();; ++s)
	{
		s = strstr(s, substr);
		if(!s)
			return -1;
		if (s[substr_len] == char_after_substr)
			return int(s - c_str());
	}
}
int WTString::Find2(char char_before_substr, const char* substr) const
{
	_ASSERTE(AfxIsValidString(substr));

	auto substr_len = strlen(substr);
	for (const char* s = c_str();; ++s)
	{
		s = strchr(s, char_before_substr);
		if (!s)
			return -1;
		if (!strncmp(s + 1, substr, substr_len))
			return int(s - c_str());
	}
}

// find a sub-string reverse dir - really find last sub-string
int WTString::ReverseFind(LPCTSTR lpszSub) const
{
	_ASSERTE(AfxIsValidString(lpszSub, FALSE));

	// find substring
	int idx, currentWinningPos = -1;
	LPTSTR lpsz = _tcsstr(m_pchData, lpszSub);
	while (lpsz)
	{
		idx = (int)(lpsz - m_pchData);
		if (idx > currentWinningPos)
		{
			currentWinningPos = idx;
		}
		lpsz = _tcsstr(lpsz + sizeof(TCHAR), lpszSub);
	}

	// return -1 for not found, distance from beginning otherwise
	return currentWinningPos;
}

// again, this is really a find last sub-string case insensitive
int WTString::ReverseFindNoCase(LPCTSTR lpszsub)
{
	_ASSERTE(AfxIsValidString(lpszsub, FALSE));

	// change case
	WTString substrToFind(lpszsub);
	substrToFind.MakeLower();

	WTString saveMe(*this);
	MakeLower();

	// find substring
	int idx, currentWinningPos = -1;
	LPTSTR lpsz = _tcsstr(m_pchData, substrToFind.c_str());
	while (lpsz)
	{
		idx = (int)(lpsz - m_pchData);
		if (idx > currentWinningPos)
		{
			currentWinningPos = idx;
		}
		lpsz = _tcsstr(lpsz + sizeof(TCHAR), substrToFind.c_str());
	}

	// return -1 for not found, distance from beginning otherwise
	*this = saveMe;
	return currentWinningPos;
}

/////////////////////////////////////////////////////////////////////////////
// WTString formatting

#ifdef _MAC
#define TCHAR_ARG int
#define WCHAR_ARG unsigned
#define CHAR_ARG int
#else
#define TCHAR_ARG TCHAR
#define WCHAR_ARG WCHAR
#define CHAR_ARG char
#endif

#if _MSC_VER >= 1200
#define DOUBLE_ARG double
#elif defined(_68K_) || defined(_X86_)
#define DOUBLE_ARG _AFX_DOUBLE
#else
#define DOUBLE_ARG double
#endif

#define FORCE_ANSI 0x10000
#define FORCE_UNICODE 0x20000

void WTString::FormatV(LPCTSTR lpszFormat, va_list argList)
{
	_ASSERTE(AfxIsValidString(lpszFormat, FALSE));

	va_list argListSave = argList;
	int nMaxLen = _vsctprintf(lpszFormat, argListSave) + 1;

	(void)GetBuffer(nMaxLen);
	VERIFY(_vstprintf(m_pchData, lpszFormat, argListSave) <= GetAllocLength());
	ReleaseBuffer();

	va_end(argListSave);
}

void WTString::AppendFormatV(LPCTSTR lpszFormat, va_list argList)
{
	_ASSERTE(AfxIsValidString(lpszFormat, FALSE));

	va_list argListSave = argList;
	int nMaxLen = _vsctprintf(lpszFormat, argListSave) + 1;

	(void)GetBuffer(GetLength() + nMaxLen);
	VERIFY(_vstprintf(m_pchData + GetLength(), lpszFormat, argListSave) <= GetAllocLength());
	ReleaseBuffer();

	va_end(argListSave);
}

// formatting (using sprintf style formatting)
WTString& AFX_CDECL WTString::__WTFormat(
#ifdef _DEBUG
    int /*dummy*/,
#endif
    LPCTSTR lpszFormat, ...)
{
	_ASSERTE(AfxIsValidString(lpszFormat, FALSE));

	va_list argList;
	va_start(argList, lpszFormat);
	FormatV(lpszFormat, argList);
	va_end(argList);

	return *this;
}

WTString& AFX_CDECL WTString::__WTAppendFormat(
#ifdef _DEBUG
    int /*dummy*/,
#endif
    LPCTSTR lpszFormat, ...)
{
	_ASSERTE(AfxIsValidString(lpszFormat, FALSE));

	va_list argList;
	va_start(argList, lpszFormat);
	AppendFormatV(lpszFormat, argList);
	va_end(argList);

	return *this;
}

WTString& AFX_CDECL WTString::Format(UINT nFormatID, ...)
{
	WTString strFormat;
	VERIFY(strFormat.LoadString(nFormatID) != 0);

	va_list argList;
	va_start(argList, nFormatID);
	FormatV(strFormat.c_str(), argList);
	va_end(argList);

	return *this;
}

#ifndef _MAC
// formatting (using FormatMessage style formatting)
void AFX_CDECL WTString::FormatMessage(LPCTSTR lpszFormat, ...)
{
	// format message into temporary buffer lpszTemp
	va_list argList;
	va_start(argList, lpszFormat);
	LPTSTR lpszTemp;

	if (::FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, lpszFormat, 0, 0,
	                    (LPTSTR)&lpszTemp, 0, &argList) == 0 ||
	    lpszTemp == NULL)
	{
		AfxThrowMemoryException();
	}

	// assign lpszTemp into the resulting string and free the temporary
	*this = lpszTemp;
	LocalFree(lpszTemp);
	va_end(argList);
}

void AFX_CDECL WTString::FormatMessage(UINT nFormatID, ...)
{
	// get format string from string table
	WTString strFormat;
	VERIFY(strFormat.LoadString(nFormatID) != 0);

	// format message into temporary buffer lpszTemp
	va_list argList;
	va_start(argList, nFormatID);
	LPTSTR lpszTemp;
	if (::FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, strFormat.c_str(), 0, 0,
	                    (LPTSTR)&lpszTemp, 0, &argList) == 0 ||
	    lpszTemp == NULL)
	{
		AfxThrowMemoryException();
	}

	// assign lpszTemp into the resulting string and free lpszTemp
	*this = lpszTemp;
	LocalFree(lpszTemp);
	va_end(argList);
}

#endif //!_MAC

bool WTString::TrimRightChar(char cut)
{
	// find beginning of trailing spaces by starting at beginning (DBCS aware)
	const char *lpsz = m_pchData;
	const char *lpszLast = NULL;
	while (*lpsz != '\0')
	{
		if (*lpsz == cut)
		{
			if (lpszLast == NULL)
				lpszLast = lpsz;
		}
		else
			lpszLast = NULL;
		++lpsz;
	}

	if (lpszLast != NULL)
	{
		auto new_len = ptr_sub__int(lpszLast, m_pchData);
		CopyBeforeWrite();

		// truncate at trailing space start
		m_pchData[new_len] = '\0';
		GetData()->nDataLength = new_len;
		return true;
	}
	else
		return false;
}

bool WTString::TrimRight()
{
	// find beginning of trailing spaces by starting at beginning (DBCS aware)
	const char *lpsz = m_pchData;
	const char *lpszLast = NULL;
	while (*lpsz != '\0')
	{
		if (wt_isspace(*lpsz))
		{
			if (lpszLast == NULL)
				lpszLast = lpsz;
		}
		else
			lpszLast = NULL;
		++lpsz;
	}

	if (lpszLast != NULL)
	{
		auto new_len = ptr_sub__int(lpszLast, m_pchData);
		CopyBeforeWrite();

		// truncate at trailing space start
		m_pchData[new_len] = '\0';
		GetData()->nDataLength = new_len;
		return true;
	}
	else
		return false;
}

bool WTString::TrimLeftChar(char cut)
{
	// find first non-space character
	const char *lpsz = m_pchData;
	while (*lpsz == cut)
		++lpsz;

	// fix up data and length
	auto cut_len = ptr_sub__int(lpsz, m_pchData);
	if (cut_len > 0)
	{
		CopyBeforeWrite();

		int nDataLength = GetData()->nDataLength - cut_len;
		memmove(m_pchData, m_pchData + cut_len, (nDataLength + 1) * sizeof(TCHAR));
		GetData()->nDataLength = nDataLength;
		return true;
	}
	else
		return false;
}

bool WTString::TrimLeft()
{
	// find first non-space character
	const char *lpsz = m_pchData;
	while (wt_isspace(*lpsz))
		++lpsz;

	// fix up data and length
	auto cut_len = ptr_sub__int(lpsz, m_pchData);
	if (cut_len > 0)
	{
		CopyBeforeWrite();

		int nDataLength = GetData()->nDataLength - cut_len;
		memmove(m_pchData, m_pchData + cut_len, (nDataLength + 1) * sizeof(TCHAR));
		GetData()->nDataLength = nDataLength;
		return true;
	}
	else
		return false;
}

bool WTString::TrimWordLeft(const char* word, bool case_insensitive /*= false*/)
{
	if (!StartsWith(c_str(), word, true, !case_insensitive))
		return false;

	*this = Mid(strlen(word));
	return true;
}

bool WTString::TrimWordRight(const char* word, bool case_insensitive /*= false*/)
{
	if (!::EndsWith(c_str(), word, true, !case_insensitive))
		return false;

	LeftInPlace(GetLength() - (int)strlen(word));
	return true;
}

int WTString::GetTokCount(char chToFind) const
{
	LPTSTR ptr = m_pchData;
	int cnt = 0;
	while (ptr && *ptr != NULL)
	{
		if (*ptr++ == chToFind)
			cnt++;
	}
	return cnt;
}

#ifdef _UNICODE
#define CHAR_FUDGE 1 // one TCHAR unused is good enough
#else
#define CHAR_FUDGE 2 // two BYTES unused for case of DBC last char
#endif

BOOL WTString::LoadString(UINT nID)
{
	// try fixed buffer first (to avoid wasting space in the heap)
	TCHAR szTemp[256];
	int nLen = AfxLoadString(nID, szTemp, _countof(szTemp));
	if (_countof(szTemp) - nLen > CHAR_FUDGE)
	{
		*this = szTemp;
		return nLen > 0;
	}

	// try buffer size of 512, then larger size until entire string is retrieved
	uint nSize = 256;
	do
	{
		nSize += 256;
		nLen = AfxLoadString(nID, GetBuffer(int(nSize - 1)), nSize);
	} while (nSize - nLen <= CHAR_FUDGE);
	ReleaseBuffer();

	return nLen > 0;
}

void WTString::insert(int p, LPCTSTR s)
{
	WTString s1 = Mid(0, p);
	WTString s2 = Mid(p);
	*this = s1 + s + s2;
}
void WTString::insert_new(size_t p, std::string_view sv)
{
	if (sv.empty())
		return;
	assert(!is_my_ptr(sv.data()));
	if(is_my_ptr(sv.data())) [[unlikely]]
	{
		// terrible performance, should never be used, but be correct
		*this = WTString({left_sv((uint32_t)p), sv, mid_sv((uint32_t)p)});
		return;
	}

	p = std::clamp<size_t>(p, 0llu, (size_t)GetLength());

	char *buf = GetBuffer(GetLength() + (int)sv.length() + 1);
	memmove(buf + p + sv.size(), buf + p, GetLength() - p + 1);
	memcpy(buf + p, sv.data(), sv.size());
	ReleaseBuffer(GetLength() + (int)sv.length());
}
void WTString::remove(size_t p, size_t l)
{
	if (p >= (uint32_t)GetLength())
		return;
	l = std::min(l, (uint32_t)GetLength() - p);

	char *buf = GetBuffer(0);
	memmove(buf + p, buf + p + l, GetLength() - p - l + 1);
	ReleaseBuffer(GetLength() - (int)l);
}

void WtStrSplitW(const CStringW& str, WideStrVector& arr, LPCWSTR delimiter)
{
	if (str.IsEmpty())
		return;
	_ASSERTE(delimiter);
	const int kDelimLen = wcslen_i(delimiter);
	int lastFoundPos = 0;
	for (int pos; (pos = str.Find(delimiter, lastFoundPos)) != NPOS; lastFoundPos = pos + kDelimLen)
		arr.push_back(str.Mid(lastFoundPos, pos + kDelimLen - lastFoundPos));

	// don't lose final fragment if present
	if (lastFoundPos < str.GetLength())
		arr.push_back(str.Mid(lastFoundPos));
}

void WtStrSplitA(const WTString& str, StrVectorA& arr, LPCSTR delimiter)
{
	if (str.IsEmpty())
		return;
	_ASSERTE(delimiter);
	const int kDelimLen = strlen_i(delimiter);
	int lastFoundPos = 0;
	for (int pos; (pos = str.Find(delimiter, lastFoundPos)) != NPOS; lastFoundPos = pos + kDelimLen)
		arr.push_back(str.Mid(lastFoundPos, pos + kDelimLen - lastFoundPos));

	// don't lose final fragment if present
	if (lastFoundPos < str.GetLength())
		arr.push_back(str.Mid(lastFoundPos));
}

WTString TokenGetField(LPCSTR str, LPCSTR sep /* = " \t\r\n"  */, int devLang /*= -1*/)
{
	if (!str)
		return WTString();
	assert(sep);
	if (sep[0] && !Is_VB_VBS_File(devLang))
	{
		if(sep[1])
		{
			if (!sep[2])
				return TokenGetField2(str, std::to_array({sep[0], sep[1]}));
		}
		else
			return TokenGetField2(str, sep[0]);
	}

	int p1, p2;
	for (p1 = 0; str[p1] && strchr(sep, str[p1]); p1++)
		;
	for (p2 = p1; str[p2] && !strchr(sep, str[p2]); p2++)
		;

	const char* continuationChar = nullptr;
	if (Is_VB_VBS_File(devLang) && p2)
	{
		if (str[p2 - 1] == '_' && !strcmp(sep, "\r\n"))
		{
			continuationChar = "_";
			while (str[p2 - 1] == '_')
			{
				p2++;
				for (; str[p2] && strchr(sep, str[p2]); p2++)
					;
				for (; str[p2] && !strchr(sep, str[p2]); p2++)
					;
			}
		}
	}

	WTString ret(&str[p1], p2 - p1);
	if (continuationChar)
	{
		ret.ReplaceAll("\r\n", " ");
		ret.ReplaceAll('\r', ' ');
		ret.ReplaceAll('\n', ' ');
		ret.ReplaceAll(continuationChar, "");
	}
	return ret;
}

std::string_view TokenGetField2(std::string_view sv, const char *sep /*= " \t\r\n"*/)
{
	std::string_view::iterator it = sv.begin();
	for(; it != sv.end() && strchr(sep, *it); ++it) {}
	std::string_view::iterator it2 = it;
	for (; it2 != sv.end() && !strchr(sep, *it2); ++it2) {}

	return {it, it2};
}
std::string_view TokenGetField2(const char *s, const char* sep /*= " \t\r\n"*/)
{
	for (; *s && strchr(sep, *s); ++s) {}
	const char *s2 = s;
	for (; *s2 && !strchr(sep, *s2); ++s2) {}

	return {s, s2};
}
void TokenGetField2InPlace(WTString& s, const char* sep /*= " \t\r\n"*/)
{
	s.MidInPlace(TokenGetField2(s.to_string_view(), sep));
}
std::string_view TokenGetField2(std::string_view sv, char sep)
{
	std::string_view::iterator it = sv.begin();
	for (; it != sv.end() && (sep == *it); ++it) {}
	std::string_view::iterator it2 = it;
	for (; it2 != sv.end() && (sep != *it2); ++it2) {}

	return {it, it2};
}
std::string_view TokenGetField2(const char *s, char sep)
{
	for (; *s && (sep == *s); ++s) {}
	const char *s2 = s;
	for (; *s2 && (sep != *s2); ++s2) {}

	return {s, s2};
}
void TokenGetField2InPlace(WTString& s, char sep)
{
	s.MidInPlace(TokenGetField2(s.to_string_view(), sep));
}

CStringW TokenGetField(LPCWSTR str, LPCWSTR sep /* = " \t\r\n"  */, int devLang /*= -1*/)
{
	if (!str)
		return CStringW();

	int p1, p2;
	for (p1 = 0; str[p1] && wcschr(sep, str[p1]); p1++)
		;
	for (p2 = p1; str[p2] && !wcschr(sep, str[p2]); p2++)
		;

	const WCHAR* continuationChar = nullptr;
	if (Is_VB_VBS_File(devLang) && p2)
	{
		if (str[p2 - 1] == L'_' && !wcscmp(sep, L"\r\n"))
		{
			continuationChar = L"_";
			while (str[p2 - 1] == L'_')
			{
				p2++;
				for (; str[p2] && wcschr(sep, str[p2]); p2++)
					;
				for (; str[p2] && !wcschr(sep, str[p2]); p2++)
					;
			}
		}
	}

	CStringW ret(&str[p1], p2 - p1);
	if (continuationChar)
	{
		ret.Replace(L"\r\n", L" ");
		ret.Replace(L'\r', L' ');
		ret.Replace(L'\n', L' ');
		ret.Replace(continuationChar, L"");
	}
	return ret;
}

LPCSTR strstrWholeWord(LPCSTR txt, LPCSTR pat, BOOL caseSensitive /*= TRUE*/)
{
	const auto strlen_pat = strlen(pat);
	while(true)
	{
		LPCSTR pos = caseSensitive ? strstr(txt, pat) : StrStrI(txt, pat);
		if (pos)
		{
			if ((txt != pos && ISCSYM(pos[-1])) || (ISCSYM(pos[strlen_pat])))
			{
				// [case: 27839] need to eat the wholeword before recursing
				while (*pos && ISCSYM(pos[1]))
					++pos;
				if (!*pos)
					return NULL;

				txt = &pos[1];
				continue;
			}
		}
		return pos;
	}
}

LPCWSTR strstrWholeWord(LPCWSTR txt, LPCWSTR pat, BOOL caseSensitive /*= TRUE*/)
{
	const auto wcslen_pat = wcslen(pat);
	while (true)
	{
		LPCWSTR pos = caseSensitive ? wcsstr(txt, pat) : StrStrIW(txt, pat);
		if (pos)
		{
			if ((txt != pos && ISCSYM(pos[-1])) || (ISCSYM(pos[wcslen_pat])))
			{
				// [case: 27839] need to eat the wholeword before recursing
				while (*pos && ISCSYM(pos[1]))
					++pos;
				if (!*pos)
					return NULL;

				txt = &pos[1];
				continue;
			}
		}
		return pos;
	}
}

int g_doDBCase = TRUE;
BOOL StartsWith(LPCSTR buf, LPCSTR begStr, BOOL wholeWord /*= TRUE*/, bool case_sensitive /*= g_doDBCase*/)
{
	if (!buf || !begStr)
		return FALSE;
	int i;
	if (/*g_doDBCase*/ case_sensitive)
		for (i = 0; buf[i] == begStr[i] && begStr[i]; i++)
			;
	else
		for (i = 0; begStr[i] &&
		            (buf[i] & 0x80 || begStr[i] & 0x80 ? buf[i] == begStr[i] : tolower(buf[i]) == tolower(begStr[i]));
		     i++)
			;
	return (!begStr[i] && (!wholeWord || !ISCSYM(buf[i])));
}
BOOL StartsWith(std::string_view buf, std::string_view begStr, BOOL wholeWord /*= true*/, bool case_sensitive /*= g_doDBCase*/)
{
	if (buf.empty() || begStr.empty())
		return false;
	if(buf.length() < begStr.length())
		return false;

	uint32_t i;
	if (/*g_doDBCase*/ case_sensitive)
		for (i = 0; (i < begStr.size()) && (buf[i] == begStr[i]); i++)
			;
	else
		for (i = 0; (i < begStr.size()) &&
		            (buf[i] & 0x80 || begStr[i] & 0x80 ? buf[i] == begStr[i] : tolower(buf[i]) == tolower(begStr[i]));
		     i++)
			;
	return (i == begStr.size()) && (!wholeWord || (buf.length() == begStr.length()) || !ISCSYM(buf[i]));
}

BOOL EndsWith(LPCSTR buf, LPCSTR endStr, BOOL wholeWord /*= TRUE*/, bool case_sensitive /*= g_doDBCase*/)
{
	if (!buf || !endStr)
		return FALSE;

	const auto buf_len = strlen(buf);
	const auto endStr_len = strlen(endStr);
	if (buf_len < endStr_len)
		return FALSE;

	bool ret = !(case_sensitive ? _strcmpi : strcmp)(buf + buf_len - endStr_len, endStr);
	if (ret && wholeWord && (buf_len > endStr_len))
		ret = !ISCSYM(buf[buf_len - endStr_len - 1]);
	return ret;
}

BOOL StartsWith(LPCWSTR buf, LPCWSTR begStr, BOOL wholeWord /*= TRUE*/, bool case_sensitive /*= g_doDBCase*/)
{
	if (!buf || !begStr)
		return FALSE;
	int i;
	if (/*g_doDBCase*/ case_sensitive)
		for (i = 0; buf[i] == begStr[i] && begStr[i]; i++)
			;
	else
		for (i = 0; towlower(buf[i]) == towlower(begStr[i]) && begStr[i]; i++)
			;
	return (!begStr[i] && (!wholeWord || !ISCSYM(buf[i])));
}

BOOL StartsWithNC(LPCSTR buf, LPCSTR begStr, BOOL wholeWord /*= TRUE*/)
{
	// No Case
	if (!buf || !begStr)
		return FALSE;
	int i;
	for (i = 0;
	     begStr[i] && (buf[i] & 0x80 || begStr[i] & 0x80 ? buf[i] == begStr[i] : tolower(buf[i]) == tolower(begStr[i]));
	     i++)
		;
	return (!begStr[i] && (!wholeWord || !ISCSYM(buf[i])));
}

BOOL StartsWithNC(LPCWSTR buf, LPCWSTR begStr, BOOL wholeWord /*= TRUE*/)
{
	// No Case
	if (!buf || !begStr)
		return FALSE;
	int i;
	for (i = 0; towlower(buf[i]) == towlower(begStr[i]) && begStr[i]; i++)
		;
	return (!begStr[i] && (!wholeWord || !ISCSYM(buf[i])));
}

bool AreSimilar(const WTString& str1, const WTString& str2)
{
	WTString tmp1(str1);
	WTString tmp2(str2);
	tmp1.MakeLower();
	tmp2.MakeLower();
	token tok1 = tmp1;
	token tok2 = tmp2;
	tok1.ReplaceAll(TRegexp("[\r\n\t ]"), "");
	tok2.ReplaceAll(TRegexp("[\r\n\t ]"), "");
	tmp1 = tok1.c_str();
	tmp2 = tok2.c_str();
	return tmp1 == tmp2;
}

void StrMatchOptions::Update(const CStringW& pattern, bool normalStrs /*= true*/, bool dbDelim /*= false*/,
                             bool supportsCaseSensitivityOption /*= true*/)
{
	mMatchStartsWith = mMatchEndsWith = false;
	mMatchPatternW = pattern;
	mIsExclusivePattern = false;
	mEatInitialCharIfDbSep = !normalStrs;

	if (mMatchPatternW.GetLength() > 1 && mMatchPatternW[0] == L'-')
	{
		mMatchPatternW = mMatchPatternW.Mid(1);
		mIsExclusivePattern = true;
	}

	if (Psettings->mEnableFilterStartEndTokens)
	{
		if (!dbDelim && mMatchPatternW.GetLength() > 1 && mMatchPatternW[0] == L'.')
		{
			mMatchPatternW = mMatchPatternW.Mid(1);
			mMatchStartsWith = true;
		}
		else if (dbDelim && mMatchPatternW.GetLength() > 1 && mMatchPatternW[0] == DB_SEP_CHR_W)
		{
			mMatchPatternW = mMatchPatternW.Mid(1);
			mMatchStartsWith = true;
		}

		if (!dbDelim && mMatchPatternW.GetLength() > 1 && mMatchPatternW[mMatchPatternW.GetLength() - 1] == L'.')
		{
			mMatchPatternW = mMatchPatternW.Left(mMatchPatternW.GetLength() - 1);
			mMatchEndsWith = true;
		}
		else if (dbDelim && mMatchPatternW.GetLength() > 1 &&
		         mMatchPatternW[mMatchPatternW.GetLength() - 1] == DB_SEP_CHR_W)
		{
			mMatchPatternW = mMatchPatternW.Left(mMatchPatternW.GetLength() - 1);
			mMatchEndsWith = true;
		}
	}

	if (supportsCaseSensitivityOption)
		mCaseSensitive = Psettings->mForceCaseInsensitiveFilters ? false : !!StrHasUpperCase(mMatchPatternW);
	else
		mCaseSensitive = false;

	mMatchPatternA = mMatchPatternW;
}

CStringW StrMatchOptions::TweakFilter(const CStringW& filter, bool forFileSearch /*= false*/, bool dbDelim /*= false*/)
{
	CStringW ret(filter);

	// trim trailing '-', ' ' or "-." so we don't unnecessarily invoke filter updates
	ret.TrimRight(L'-');
	if (Psettings->mEnableFilterStartEndTokens)
	{
		if (ret.GetLength() > 1 && ret[ret.GetLength() - 1] == L'.' && ret[ret.GetLength() - 2] == L'-')
			ret = ret.Left(ret.GetLength() - 2);
		if (ret.GetLength() > 1 && ret[ret.GetLength() - 1] == L'.' && ret[ret.GetLength() - 2] == L' ')
			ret = ret.Left(ret.GetLength() - 2);
	}
	ret.Trim();
	if (Psettings->mEnableFilterStartEndTokens && ret == L".")
		ret.Empty();

	ret.Replace(L"::", L".");

	_ASSERTE(!forFileSearch || !dbDelim);
	if (dbDelim)
		ret.Replace(L'.', DB_SEP_CHR_W);
	else if (!forFileSearch)
		ret.Replace(DB_SEP_CHR_W, L'.');

	return ret;
}


template <typename CH>
inline bool wt_isupper(CH c)
{
	return (c >= CH{'A'}) & (c <= CH{'Z'});
}
template <typename CH>
inline CH wt_tolower(CH c)
{
	return wt_isupper(c) ? CH(c - 'A' + 'a') : c;
}
template <typename CH>
inline bool wt_islower(CH c)
{
	return (c >= CH{'a'}) & (c <= CH{'z'});
}
template <typename CH>
inline CH wt_toupper(CH c)
{
	return wt_islower(c) ? CH(c - 'a' + 'A') : c;
}

template<typename CH>
void StrMatchOptionsT<CH>::Update(std::basic_string_view<CH> pattern, bool normalStrs /*= true*/, bool dbDelim /*= false*/,
                             bool supportsCaseSensitivityOption /*= true*/)
{
	mMatchStartsWith = mMatchEndsWith = false;
	mMatchPattern = pattern;
	mIsExclusivePattern = false;
	mEatInitialCharIfDbSep = !normalStrs;

	mMatchPattern_uppercase.resize(mMatchPattern.size());
	std::transform(mMatchPattern.cbegin(), mMatchPattern.cend(), mMatchPattern_uppercase.begin(), wt_toupper<CH>);

	if ((mMatchPattern.size() > 1) && mMatchPattern.starts_with(CH{'-'}))
	{
		mMatchPattern.erase(mMatchPattern.begin());
		mIsExclusivePattern = true;
	}

	if (Psettings->mEnableFilterStartEndTokens)
	{
		if (!dbDelim && (mMatchPattern.size() > 1) && mMatchPattern.starts_with(CH{'.'}))
		{
			mMatchPattern.erase(mMatchPattern.begin());
			mMatchStartsWith = true;
		}
		else if (dbDelim && (mMatchPattern.size() > 1) && mMatchPattern.starts_with(CH{DB_SEP_CHR}))
		{
			mMatchPattern.erase(mMatchPattern.begin());
			mMatchStartsWith = true;
		}

		if (!dbDelim && (mMatchPattern.size() > 1) && mMatchPattern.ends_with(CH{'.'}))
		{
			mMatchPattern.resize(mMatchPattern.size() - 1);
			mMatchEndsWith = true;
		}
		else if (dbDelim && (mMatchPattern.size() > 1) && mMatchPattern.ends_with(CH{DB_SEP_CHR_W}))
		{
			mMatchPattern.resize(mMatchPattern.size() - 1);
			mMatchEndsWith = true;
		}
	}

	static const auto StrHasUpperCase = [](std::basic_string_view<CH> sv) {
		return std::find_if(sv.cbegin(), sv.cend(), 
							[](CH c) { return std::clamp(c, CH{'A'}, CH{'Z'}) == c; }
		) != sv.cend();
	};
	if (supportsCaseSensitivityOption)
		mCaseSensitive = Psettings->mForceCaseInsensitiveFilters ? false : !!StrHasUpperCase(mMatchPattern);
	else
		mCaseSensitive = false;
}

template<typename CH>
StrMatchOptionsT<CH>::StrMatchOptionsT(std::basic_string_view<CH> pattern, bool normalStrs, bool dbDelim, bool supportsCaseSensitivityOption)
{
	Update(pattern, normalStrs, dbDelim, supportsCaseSensitivityOption);

	if(Psettings)
	{
		if (Psettings->mEnableFuzzyFilters && (Psettings->mFuzzyFiltersThreshold > 0))
			mFuzzy = Psettings->mFuzzyFiltersThreshold;
		mFuzzyLite = Psettings->mEnableFuzzyLite;
		mEnableFuzzyMultiThreading = Psettings->mEnableFuzzyMultiThreading;
	}
}

template <typename CH>
uint32_t StrMatchOptionsT<CH>::GetFuzzy() const
{
	if (!mFuzzy || !*mFuzzy)
		return 0;
	if (mCaseSensitive || mIsExclusivePattern || mMatchStartsWith || mMatchEndsWith)
		return 0;
	return std::clamp(*mFuzzy, 1u, 100u);
}
template <typename CH>
bool StrMatchOptionsT<CH>::GetFuzzyLite() const
{
	if (mCaseSensitive || mIsExclusivePattern || mMatchStartsWith || mMatchEndsWith)
		return false;
	return mFuzzyLite;
}

template <typename CH>
std::basic_string<CH> StrMatchOptionsT<CH>::TweakFilter2(std::basic_string<CH> filter, bool forFileSearch /*= false*/, bool dbDelim /*= false*/)
{
	// trim trailing '-', ' ' or "-." so we don't unnecessarily invoke filter updates
	trim_right(filter, CH{'-'});
	if (Psettings->mEnableFilterStartEndTokens)
	{
		static const std::basic_string<CH> minus_dot_str{CH{'-'}, CH{'.'}};
		static const std::basic_string<CH> space_dot_str{CH{' '}, CH{'.'}};
		if (filter.ends_with(minus_dot_str))
			filter.resize(filter.size() - 2);
		if (filter.ends_with(space_dot_str))
			filter.resize(filter.size() - 2);
	}
	trim(filter);

	static const std::basic_string<CH> dot_str{CH{'.'}};
	if (Psettings->mEnableFilterStartEndTokens && (filter == dot_str))
		filter.clear();

	//filter.Replace(L"::", L".");
	static const std::basic_string<CH> scope_str{CH{':'}, CH{':'}};
	replace_all<CH>(filter, scope_str, dot_str);

	_ASSERTE(!forFileSearch || !dbDelim);
	static const std::basic_string<CH> db_sep_str{CH{DB_SEP_CHR}};
	if (dbDelim)
		replace_all<CH>(filter, dot_str, db_sep_str);
	else if (!forFileSearch)
		replace_all<CH>(filter, db_sep_str, dot_str);

	return filter;
}
template struct StrMatchOptionsT<char>;
template struct StrMatchOptionsT<wchar_t>;

int StrMatchRankedA(const LPCSTR str, const StrMatchOptions& opt)
{
	_ASSERTE(opt.mMatchPatternA.Find(' ') == -1);
	int rank = opt.mIsExclusivePattern ? 1 : 0;
	uint len = (uint)opt.mMatchPatternA.GetLength();
	if (!len)
		return rank;

	const char uc1 = len < 1                        ? '\0'
	                 : opt.mMatchPatternA[0] & 0x80 ? opt.mMatchPatternA[0]
	                                                : (char)toupper(opt.mMatchPatternA[0]);
	const char lc1 = len < 1                        ? '\0'
	                 : opt.mMatchPatternA[0] & 0x80 ? opt.mMatchPatternA[0]
	                                                : (char)tolower(opt.mMatchPatternA[0]);
	const char uc2 = len < 2                        ? '\0'
	                 : opt.mMatchPatternA[1] & 0x80 ? opt.mMatchPatternA[1]
	                                                : (char)toupper(opt.mMatchPatternA[1]);
	const char lc2 = len < 2                        ? '\0'
	                 : opt.mMatchPatternA[1] & 0x80 ? opt.mMatchPatternA[1]
	                                                : (char)tolower(opt.mMatchPatternA[1]);

	for (LPCSTR p = str; *p && *p != DB_FIELD_DELIMITER; p++)
	{
		if ((*p != uc1) && (*p != lc1))
			continue;
		if (uc2 && (p[1] != uc2) && (p[1] != lc2))
			continue;

		if (opt.mCaseSensitive)
		{
			if (_tcsncmp(p, opt.mMatchPatternA.c_str(), len))
				continue;
		}
		else
		{
			if (_tcsnicmp(p, opt.mMatchPatternA.c_str(), len))
				continue;
		}

		bool startsWith = false; // start of a word in the str
		bool endsWith = false;
		bool startsAtBeginning = false; // start of whole str as opposed to start of a word in the str
		if (p == str || (opt.mEatInitialCharIfDbSep && p == &str[1] && str[0] == DB_SEP_CHR))
			startsAtBeginning = startsWith = true;
		else if (p[-1] && !(ISCSYM(p[-1])))
			startsWith = true; // prev char was a wordbreak

		if (opt.mMatchEndsWith)
		{
			// perf: only check for ends with if endsWith option is set
			LPCSTR p2;
			for (p2 = p; *p2 && *p2 != DB_FIELD_DELIMITER; p2++)
				;

			int p2Len = ptr_sub__int(p2, p);
			if (p2Len == opt.mMatchPatternA.GetLength())
				endsWith = true;
			else if (p2Len > opt.mMatchPatternA.GetLength() &&
			         (!p[opt.mMatchPatternA.GetLength()] || !(ISCSYM(p[opt.mMatchPatternA.GetLength()]))))
				endsWith = true;
		}

		if (opt.mMatchStartsWith && opt.mMatchEndsWith)
		{
			if (endsWith)
			{
				if (startsAtBeginning)
					rank = opt.mIsExclusivePattern ? 0 : 5;
				else if (startsWith)
					rank = opt.mIsExclusivePattern ? 0 : 4;
			}
		}
		else if (opt.mMatchEndsWith)
		{
			if (endsWith)
			{
				if (startsAtBeginning)
					rank = opt.mIsExclusivePattern ? 0 : 5;
				else if (startsWith)
					rank = opt.mIsExclusivePattern ? 0 : 4;
				else
					rank = opt.mIsExclusivePattern ? 0 : 3;
			}
		}
		else if (opt.mMatchStartsWith)
		{
			if (startsAtBeginning && endsWith)
				rank = opt.mIsExclusivePattern
				           ? 0
				           : 5; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsAtBeginning)
				rank = opt.mIsExclusivePattern ? 0 : 4;
			else if (startsWith && endsWith)
				rank = opt.mIsExclusivePattern
				           ? 0
				           : 3; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsWith)
				rank = opt.mIsExclusivePattern ? 0 : 2;
		}
		else if (opt.mIsExclusivePattern)
		{
			rank = 0; // contains, so exclude
		}
		else
		{
			// inclusive hit
			if (startsAtBeginning && endsWith)
				rank = 5; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsAtBeginning)
				rank = 4;
			else if (startsWith && endsWith)
				rank = 3; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsWith)
				rank = 2;
			else
				rank = 1; // contains
		}

		if (opt.mIsExclusivePattern)
		{
			if (rank == 0)
				break;
		}
		else
		{
			// inclusive
			if (0 != rank)
				break;
		}
	}

	return rank;
}

int StrMatchRankedW(const LPCWSTR str, const StrMatchOptions& opt, bool matchBothSlashAndBackslash)
{
	_ASSERTE(opt.mMatchPatternW.Find(L' ') == -1);
	int rank = opt.mIsExclusivePattern ? 1 : 0;
	uint len = (uint)opt.mMatchPatternW.GetLength();
	if (!len)
		return rank;

	// case 148282 [ofis] allow / as well as \ to trigger path filtering
	auto convertToBackslashIfEnabled = [matchBothSlashAndBackslash](const WCHAR ch) {
		if (matchBothSlashAndBackslash && ch == L'/')
			return L'\\';
		else
			return ch;
	};

	const WCHAR uc1 = len < 1 ? L'\0' : convertToBackslashIfEnabled((WCHAR)towupper(opt.mMatchPatternW[0]));
	const WCHAR lc1 = len < 1 ? L'\0' : convertToBackslashIfEnabled((WCHAR)towlower(opt.mMatchPatternW[0]));
	const WCHAR uc2 = len < 2 ? L'\0' : convertToBackslashIfEnabled((WCHAR)towupper(opt.mMatchPatternW[1]));
	const WCHAR lc2 = len < 2 ? L'\0' : convertToBackslashIfEnabled((WCHAR)towlower(opt.mMatchPatternW[1]));

	for (LPCWSTR p = str; *p && *p != DB_FIELD_DELIMITER_W; p++)
	{
		if (*p == uc1 || *p == lc1)
		{
			if (!uc2 || p[1] == uc2 || p[1] == lc2)
			{
				if (opt.mCaseSensitive)
				{
					if (wcsncmp(p, opt.mMatchPatternW, len) != 0)
						continue;
				}
				else
				{
					if (_wcsnicmp(p, opt.mMatchPatternW, len) != 0)
					{
						if (!matchBothSlashAndBackslash)
							continue;

						// case 148282 [ofis] allow / as well as \ to trigger path filtering
						CStringW matchPatternW = opt.mMatchPatternW;
						matchPatternW.Replace(L"/", L"\\");
						if (_wcsnicmp(p, matchPatternW, (uint)matchPatternW.GetLength()) != 0)
							continue;
					}
				}

				bool startsWith = false; // start of a word in the str
				bool endsWith = false;
				bool startsAtBeginning = false; // start of whole str as opposed to start of a word in the str
				if (p == str || (opt.mEatInitialCharIfDbSep && p == &str[1] && str[0] == DB_SEP_CHR))
					startsAtBeginning = startsWith = true;
				else if (p[-1] && !(ISCSYM(p[-1])))
					startsWith = true; // prev char was a wordbreak

				if (opt.mMatchEndsWith)
				{
					// perf: only check for ends with if endsWith option is set
					LPCWSTR p2;
					for (p2 = p; *p2 && *p2 != DB_FIELD_DELIMITER_W; p2++)
						;

					int p2Len = ptr_sub__int(p2, p);
					if (p2Len == opt.mMatchPatternW.GetLength())
						endsWith = true;
					else if (p2Len > opt.mMatchPatternW.GetLength() &&
					         (!p[opt.mMatchPatternW.GetLength()] || !(ISCSYM(p[opt.mMatchPatternW.GetLength()]))))
						endsWith = true;
				}

				if (opt.mMatchStartsWith && opt.mMatchEndsWith)
				{
					if (endsWith)
					{
						if (startsAtBeginning)
							rank = opt.mIsExclusivePattern ? 0 : 5;
						else if (startsWith)
							rank = opt.mIsExclusivePattern ? 0 : 4;
					}
				}
				else if (opt.mMatchEndsWith)
				{
					if (endsWith)
					{
						if (startsAtBeginning)
							rank = opt.mIsExclusivePattern ? 0 : 5;
						else if (startsWith)
							rank = opt.mIsExclusivePattern ? 0 : 4;
						else
							rank = opt.mIsExclusivePattern ? 0 : 3;
					}
				}
				else if (opt.mMatchStartsWith)
				{
					if (startsAtBeginning && endsWith)
						rank = opt.mIsExclusivePattern
						           ? 0
						           : 5; // not possible due to endsWith only being checked if opt.mMatchEndsWith
					else if (startsAtBeginning)
						rank = opt.mIsExclusivePattern ? 0 : 4;
					else if (startsWith && endsWith)
						rank = opt.mIsExclusivePattern
						           ? 0
						           : 3; // not possible due to endsWith only being checked if opt.mMatchEndsWith
					else if (startsWith)
						rank = opt.mIsExclusivePattern ? 0 : 2;
				}
				else if (opt.mIsExclusivePattern)
				{
					rank = 0; // contains, so exclude
				}
				else
				{
					// inclusive hit
					if (startsAtBeginning && endsWith)
						rank = 5; // not possible due to endsWith only being checked if opt.mMatchEndsWith
					else if (startsAtBeginning)
						rank = 4;
					else if (startsWith && endsWith)
						rank = 3; // not possible due to endsWith only being checked if opt.mMatchEndsWith
					else if (startsWith)
						rank = 2;
					else
						rank = 1; // contains
				}

				if (opt.mIsExclusivePattern)
				{
					if (rank == 0)
						break;
				}
				else
				{
					// inclusive
					if (0 != rank)
						break;
				}
			}
		}
	}

	return rank;
}



template<typename CH>
double rapidfuzz__fuzz__partial_ratio(std::basic_string_view<CH> string, std::basic_string_view<CH> pattern, double score_cutoff);

template <typename CH>
int8_t StrMatchRankedT(std::basic_string_view<CH> str, const StrMatchOptionsT<CH>& opt, StrMatchTempStorageT<CH> &temp_storage, bool matchBothSlashAndBackslash /*= false*/)
{
	size_t s = 0;
	for (const CH* p = str.data(); s < str.size(); ++s)
	{
// due to ItemToText optimization, str length might be invalid, so we need to check for \0 as well
		if ((p[s] == CH{'\0'}) | (p[s] == CH{DB_FIELD_DELIMITER})) [[unlikely]]
			break;
	}
	str = str.substr(0, s);

	const auto str_orig = str;
	_ASSERTE(opt.mMatchPattern.find(CH{' '}) == -1);
	int8_t rank = opt.mIsExclusivePattern ? 1 : 0;
	size_t len = (uint)opt.mMatchPattern.size();
	if (!len)
		return rank;

	// case 148282 [ofis] allow / as well as \ to trigger path filtering
	auto convertToBackslashIfEnabled = [matchBothSlashAndBackslash](CH ch) {
		if (matchBothSlashAndBackslash && (ch == CH{'/'}))
			return CH{'\\'};
		else
			return ch;
	};

	const CH uc1 = (len < 1) ? CH{'\0'} : convertToBackslashIfEnabled(wt_toupper<CH>(opt.mMatchPattern[0]));
	const CH lc1 = (len < 1) ? CH{'\0'} : convertToBackslashIfEnabled(wt_tolower<CH>(opt.mMatchPattern[0]));
	const CH uc2 = (len < 2) ? CH{'\0'} : convertToBackslashIfEnabled(wt_toupper<CH>(opt.mMatchPattern[1]));
	const CH lc2 = (len < 2) ? CH{'\0'} : convertToBackslashIfEnabled(wt_tolower<CH>(opt.mMatchPattern[1]));

	for (bool first = true; !str.empty() /*&& str[0] && !str.starts_with(CH{DB_FIELD_DELIMITER})*/; str = str.substr(1), first = false)
	{
		if (!str.starts_with(uc1) && !str.starts_with(lc1))
			continue;
		if (uc2 && !str.substr(1).starts_with(uc2) && !str.substr(1).starts_with(lc2))
			continue;

		if (opt.mCaseSensitive)
		{
			if(!str.starts_with(opt.mMatchPattern))
				continue;
		}
		else
		{
			if (!std::ranges::starts_with(str, opt.mMatchPattern, {}, wt_tolower<CH>, wt_tolower<CH>))
			{
				if (!matchBothSlashAndBackslash)
					continue;

				// case 148282 [ofis] allow / as well as \ to trigger path filtering
				if(temp_storage.matchPattern.empty())
				{
					temp_storage.matchPattern = opt.mMatchPattern;
					static const std::basic_string<CH> slash_str{CH{'/'}};
					static const std::basic_string<CH> backslash_str{CH{'\\'}};
					replace_all<CH>(temp_storage.matchPattern, slash_str, backslash_str);
				}
				if (!std::ranges::starts_with(str, temp_storage.matchPattern, {}, wt_tolower<CH>, wt_tolower<CH>))
					continue;
			}
		}

		bool startsWith = false; // start of a word in the str
		bool endsWith = false;
		bool startsAtBeginning = false; // start of whole str as opposed to start of a word in the str
		if (first || (opt.mEatInitialCharIfDbSep && (str.data() == (str_orig.data() + 1)) && str_orig.starts_with(CH{DB_SEP_CHR})))
			startsAtBeginning = startsWith = true;
		else if (str.data()[-1] && !(ISCSYM(str.data()[-1])))
			startsWith = true; // prev char was a wordbreak

		if (opt.mMatchEndsWith)
		{
			// perf: only check for ends with if endsWith option is set
			auto i = str.find(CH{DB_FIELD_DELIMITER});
			auto p2Len = (i != std::basic_string_view<CH>::npos) ? i : str.size();
			if (p2Len == opt.mMatchPattern.size())
				endsWith = true;
			else if (p2Len > opt.mMatchPattern.size() &&
					    (!str[opt.mMatchPattern.size()] || !(ISCSYM(str[opt.mMatchPattern.size()]))))
				endsWith = true;
		}

		if (opt.mMatchStartsWith && opt.mMatchEndsWith)
		{
			if (endsWith)
			{
				if (startsAtBeginning)
					rank = opt.mIsExclusivePattern ? 0 : 5;
				else if (startsWith)
					rank = opt.mIsExclusivePattern ? 0 : 4;
			}
		}
		else if (opt.mMatchEndsWith)
		{
			if (endsWith)
			{
				if (startsAtBeginning)
					rank = opt.mIsExclusivePattern ? 0 : 5;
				else if (startsWith)
					rank = opt.mIsExclusivePattern ? 0 : 4;
				else
					rank = opt.mIsExclusivePattern ? 0 : 3;
			}
		}
		else if (opt.mMatchStartsWith)
		{
			if (startsAtBeginning && endsWith)
				rank = opt.mIsExclusivePattern
						    ? 0
						    : 5; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsAtBeginning)
				rank = opt.mIsExclusivePattern ? 0 : 4;
			else if (startsWith && endsWith)
				rank = opt.mIsExclusivePattern
						    ? 0
						    : 3; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsWith)
				rank = opt.mIsExclusivePattern ? 0 : 2;
		}
		else if (opt.mIsExclusivePattern)
		{
			rank = 0; // contains, so exclude
		}
		else
		{
			// inclusive hit
			if (startsAtBeginning && endsWith)
				rank = 5; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsAtBeginning)
				rank = 4;
			else if (startsWith && endsWith)
				rank = 3; // not possible due to endsWith only being checked if opt.mMatchEndsWith
			else if (startsWith)
				rank = 2;
			else
				rank = 1; // contains
		}

		if (opt.mIsExclusivePattern)
		{
			if (rank == 0)
				break;
		}
		else
		{
			// inclusive
			if (0 != rank)
				break;
		}
	}

	if(!rank && !opt.mIsExclusivePattern && (str_orig.size() > 2) && (opt.mMatchPattern.size() > 2))
	{
		uint32_t fuzzy = opt.GetFuzzy();
		if (fuzzy)
		{
			try
			{
				temp_storage.s2.resize(str_orig.size());
				std::transform(str_orig.cbegin(), str_orig.cend(), temp_storage.s2.begin(), wt_tolower<CH>);

				if(temp_storage.pat.empty())
				{
					temp_storage.pat.resize(opt.mMatchPattern.size());
					std::transform(opt.mMatchPattern.cbegin(), opt.mMatchPattern.cend(), temp_storage.pat.begin(), wt_tolower<CH>);
				}

				double score = rapidfuzz__fuzz__partial_ratio(std::basic_string_view<CH>(temp_storage.s2), std::basic_string_view<CH>(temp_storage.pat), fuzzy);
				//				::OutputDebugStringA(std::format("FEC: '{0}' in '{1}': {2}", std::string_view{opt.mMatchPatternA}, sv, score).c_str());
				if (score >= fuzzy)
					rank = 1;
			} catch(const std::exception &e)
			{
				std::ignore = e;
			}
		}

		if (opt.GetFuzzyLite())
		{
			// try to match significant first letters.
			// for example, tbb will match ThreadingBuildingBlocks and threading_building_blocks

			temp_storage.s2 = str_orig.substr(0, str_orig.find_first_of(DB_FIELD_DELIMITER));
			auto s_orig_size = temp_storage.s2.size();
			// allow_next is initially true to include the first letter
			std::erase_if(temp_storage.s2, [allow_next = true](CH& c) mutable {
				if (allow_next)
				{
					allow_next = false;
					c = wt_toupper(c);
					return false;
				}

				if (wt_isupper(c))
					return false;

				if (c == '_')
					allow_next = true;
				return true;
			});

			if ((temp_storage.s2.size() >= 2) && (s_orig_size > temp_storage.s2.size()))
			{
				if (temp_storage.s2.cend() != std::search(temp_storage.s2.cbegin(), temp_storage.s2.cend(), opt.mMatchPattern_uppercase.cbegin(), opt.mMatchPattern_uppercase.cend()))
					rank = 1;
			}
		}
	}

	return rank;
}
template int8_t StrMatchRankedT<char>(std::basic_string_view<char> str, const StrMatchOptionsT<char>& opt, StrMatchTempStorageT<char>& temp_storage, bool matchBothSlashAndBackslash /*= false*/);
template int8_t StrMatchRankedT<wchar_t>(std::basic_string_view<wchar_t> str, const StrMatchOptionsT<wchar_t>& opt, StrMatchTempStorageT<wchar_t>& temp_storage, bool matchBothSlashAndBackslash /*= false*/);

bool StrIsMultiMatchPattern(LPCSTR pat)
{
	return StrIsMultiMatchPattern(CStringW(pat));
}

bool StrIsMultiMatchPattern(const CStringW& pat)
{
	return pat.FindOneOf(L" ,") != -1;
}

static int DoStrMultiMatchRanked(LPCSTR str, LPCSTR pat)
{
	int rank = 0;
	WTString subPat;
	token patTok = pat;
	while (patTok.more())
	{
		subPat = patTok.read(" ");
		if (subPat.IsEmpty())
			break;

		const int curPatRank = StrMatchRankedA(str, StrMatchOptions(CStringW(subPat.Wide())));
		if (!curPatRank)
			return 0;
		else if (!rank)
			rank = curPatRank; // only possible for first pattern
		else
			rank = min(rank, curPatRank);
	}
	return rank;
}

int StrMultiMatchRanked(LPCSTR str, LPCSTR pat)
{
	token patTok = pat;
	while (patTok.more())
	{
		WTString subPat = patTok.read(",");
		if (!subPat.IsEmpty())
		{
			int found = DoStrMultiMatchRanked(str, subPat.c_str());
			if (found)
				return found;
		}
	}

	return 0;
}

static int DoStrMultiMatchRanked(const CStringW& str, const CStringW& pat, bool matchBothSlashAndBackslash)
{
	int rank = 0;
	CStringW subPat;
	TokenW patTok = pat;
	while (patTok.more())
	{
		subPat = patTok.read(L" ");
		if (subPat.IsEmpty())
			break;

		StrMatchOptions opts(subPat);
		const int curPatRank = StrMatchRankedW(str, opts, matchBothSlashAndBackslash);
		if (!curPatRank)
			return 0;
		else if (!rank)
			rank = curPatRank; // only possible for first pattern
		else
			rank = min(rank, curPatRank);
	}
	return rank;
}

int StrMultiMatchRanked(const CStringW& str, const CStringW& pat, bool matchBothSlashAndBackslash)
{
	TokenW patTok = pat;
	while (patTok.more())
	{
		CStringW subPat = patTok.read(L",");
		if (!subPat.IsEmpty())
		{
			int found = DoStrMultiMatchRanked(str, subPat, matchBothSlashAndBackslash);
			if (found)
				return found;
		}
	}

	return 0;
}

int GetStrWidthEx(LPCSTR str, int (&widths)[256])
{
	int w = 0;
	UINT c;
	for (int i = 0; (c = (UINT)(byte)str[i]) != 0; i++)
		w += (c < 255) ? widths[c] : widths[' '];
	return w + 10;
}
int GetStrWidth(LPCSTR str)
{
	int w = 0;
	UINT c;
	for (int i = 0; (c = (UINT)(byte)str[i]) != 0; i++)
		w += (c < 255) ? g_vaCharWidth[c] : g_vaCharWidth[' '];
	return w;
}

WTString DBColonToSepStr(LPCSTR p)
{
	WTString scope(DB_SEP_CHR);
	if (p[0] == ':' || *p == DB_SEP_CHR)
		p++;
	for (; *p; p++)
	{
		if (*p == '.' || *p == ':')
			scope += DB_SEP_CHR;
		else
			scope += *p;
	}
	return scope;
}

#ifdef _DEBUG
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(0) // reset
#endif
