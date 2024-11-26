#pragma once

#ifndef NO_REMOTE_HEAP
#ifdef _DEBUG
// if enabled, assert will fire if you dealloc block that is still mapped
#define REMOTEHEAP_TRACK_MAPPED_BLOCKS
#endif
#if !defined(VA_CPPUNIT)
//#		ifdef _DEBUG
#define REMOTEHEAP_COLLECT_STATS
#define REMOTEHEAP_COLLECT_STATS_NO_TIMING
//#			define REMOTEHEAP_TIMING_PROJLOAD_AND_FINDREF
//#			define REMOTEHEAP_SHOW_LEAKS
//#		endif
#endif
#endif

#include <array>

namespace remote_heap
{
static const inline size_t min_block_size = 256;
static const inline size_t max_block_size = 65536;
static const constexpr size_t area_sizes = size_t(std::bit_width(max_block_size) - std::bit_width(min_block_size) + 1);
} // namespace remote_heap

namespace remote_heap::stats
{
// tag why sdata_map was instantiated
enum class reason : uint32_t
{
	me,
	discard,
	get_used_blocks,
	make_copy,
	make_empty,
	//	get_ScopeHash,
	//	get_SymHash,
	//	get_FileId,
	//	get_Line,
	//	get_mLine,
	//	get_GetDbSplit,
	//	get_GetDbOffset,
	//	mTypeAndDbFlags,
	//	mAttributes,
	//	mScopeHash,
	//	mSymHash,
	// HasData,
	// HasDef,
	HasSameSymData,
	SetStrs,
	GetStrs,
	DoGetStrs,
	SetDef,

	__reason_end
};

enum class stat : uint32_t
{
	areas = (uint32_t)reason::__reason_end, // number of areas alive/number of filemaps
	areas_largefilemap,                     // number of areas alive with large filemaps
	areas_max,                              // max number of areas
	areas_mapped,                           // areas currently mapped
	areas_mapped_max,                       // maximum number of areas mapped

	areas_mapped_placeholder_total,
	areas_mapped_classic_total,
	areas_mapped_placeholder,
	areas_mapped_placeholder_max,
	areas_mapped_classic,
	areas_mapped_classic_max,

	areas_pool_full_x,
	areas_pool_partial_x = areas_pool_full_x + area_sizes,
	areas_pool_empty_x = areas_pool_partial_x + area_sizes,
	areas_pool_mapped = areas_pool_empty_x + area_sizes,
	areas_pool_lru,
	areas_pool_empty_to_partial_transitions,
	areas_pool_partial_to_empty_transitions,
	areas_pool_partial_to_full_transitions,
	areas_pool_full_to_partial_transitions,
	areas_pool_expunged_from_lru,
	areas_pool_efficiency_alread_mapped,
	areas_pool_efficiency_lru,
	areas_pool_efficiency_need_to_map,
	areas_lru_unmap_pending,
	areas_lru_unmap_pending_max,

	blocks,                             // blocks used/allocated
	blocks_max,                         // max number of used blocks
	blocks_available,                   // total blocks in all areas (used or unused)
	blocks_bytes,                       // memory consumption by allocated blocks
	blocks_total_allocations,           // number of blocks allocations ever done
	blocks_total_allocation_iterations, // number of iterations needed to find free block
	blocks_mapped,                      // blocks currently mapped
	blocks_mapped_max,                  // max number of currently mapped blocks
	dealloc_lock_retries,

	blocks_reads,       // read operations on blocks
	blocks_read_bytes,  // number of bytes read from blocks
	blocks_writes,      // write operations on blocks
	blocks_write_bytes, // number of bytes written into blocks

	dtypes,       // dtypes currently allocated (includes defobjs)
	dtypes_move,  // dtypes moved
	dtypes_copy,  // dtypes copied
	dtypes_bytes, // estimated memory consumed (without padding)
	dtypes_had_no_strings,
	defobjs,

	sdatamap_block_reused,
	sdatamap_block_not_reused,

	timing_CreateFileMapping_us,
	timing_CreateFileMapping_cnt,
	timing_MapViewOfFile_us,
	timing_MapViewOfFile_cnt,
	timing_UnmapViewOfFile_us,
	timing_UnmapViewOfFile_cnt,
	timing_sdatamap_copyfrom_us,
	timing_sdatamap_copyfrom_cnt,
	timing_sdatamap_addstrings_us,
	timing_sdatamap_addstrings_cnt,
	timing_mapped_block_constructor_us,
	timing_mapped_block_constructor_cnt,
	timing_mapped_block_destructor_us,
	timing_mapped_block_destructor_cnt,
	timing_GetStrs_us,
	timing_GetStrs_cnt,
	timing_DBOut_us,
	timing_DBOut_cnt,
	timing_DType_copy_us,
	timing_DType_copy_cnt,

	timing_area_cs_us,
	timing_area_cs_cnt,
	area_cs_waiting,
	area_cs_waiting_max,
	timing_heap_cs_us,
	timing_heap_cs_cnt,
	heap_cs_waiting,
	heap_cs_waiting_max,
	timing_heap_mapped_cs_us,
	timing_heap_mapped_cs_cnt,
	heap_mapped_cs_waiting,
	heap_mapped_cs_waiting_max,
	read_wait,
	write_wait,

	__stat_end
};
inline stat operator+(stat s, size_t o)
{
	return stat((size_t)s + o);
}
#ifdef REMOTEHEAP_COLLECT_STATS
[[nodiscard]] inline std::atomic<int64_t>& get_stat(stat s)
{
	static std::atomic<int64_t> stats[(size_t)stat::__stat_end] = {0};
	assert(s < stat::__stat_end);
	return stats[(size_t)s];
}
[[nodiscard]] inline std::array<int64_t, (size_t)stat::__stat_end> get_stats_copy()
{
	std::array<int64_t, (size_t)stat::__stat_end> ret;
	for (size_t i = 0; i < (size_t)stat::__stat_end; i++)
		ret[i] = get_stat((stat)i);
	return ret;
}
inline void clear_stats()
{
	for (size_t i = 0; i < (size_t)stat::__stat_end; i++)
		get_stat((stat)i) = 0;
}
inline int64_t increase_counter(bool condition, stat s, int64_t cnt = 1)
{
	if (!condition)
		return 0;
	return get_stat(s) += cnt;
}
inline int64_t increase_counter(stat s, int64_t cnt)
{
	return increase_counter(true, s, cnt);
}
inline size_t increase_counter(stat s, size_t cnt)
{
	return (size_t)increase_counter(s, (int64_t)cnt);
}
inline int64_t increase_counter(stat s)
{
	return (int64_t)increase_counter(s, (size_t)1u);
}
inline int64_t decrease_counter(bool condition, stat s, int64_t cnt = 1)
{
	return increase_counter(condition, s, -cnt);
}
inline int64_t decrease_counter(stat s, int64_t cnt)
{
	return decrease_counter(true, s, cnt);
}
inline size_t decrease_counter(stat s, size_t cnt)
{
	return (size_t)decrease_counter(s, (int64_t)cnt);
}
inline int64_t decrease_counter(stat s)
{
	return (int64_t)decrease_counter(s, (size_t)1u);
}
inline void track_minmax(stat s_min, stat s_max, int64_t value)
{
	auto& maximum_value = get_stat(s_max);
	int64_t prev_value = maximum_value;
	bool first_time = prev_value == 0;
	while ((prev_value < value) && !maximum_value.compare_exchange_weak(prev_value, value))
	{
	}

	auto& minimum_value = get_stat(s_min);
	prev_value = minimum_value;
	first_time &= prev_value == 0;
	while (((prev_value > value) || (first_time && (prev_value == 0) && (value != 0))) &&
	       !minimum_value.compare_exchange_weak(prev_value, value))
	{
	}
}
inline void track_max(stat s_max, stat s_value)
{
	auto& maximum_value = get_stat(s_max);
	int64_t value = get_stat(s_value);
	int64_t prev_value = maximum_value;
	while ((prev_value < value) && !maximum_value.compare_exchange_weak(prev_value, value))
	{
	}
}
inline void track_avg(stat s_accu, stat s_cnt, int64_t value)
{
	increase_counter(value != 0, s_accu);
	increase_counter(s_cnt);
}
void ManageRemoteHeapStatsConsole(bool open);
#else
inline void increase_counter(bool condition, stat s, int64_t cnt = 1)
{
}
inline void increase_counter(stat s, int64_t cnt)
{
}
inline void increase_counter(stat s, size_t cnt)
{
}
inline void increase_counter(stat s)
{
}
inline void decrease_counter(bool condition, stat s, int cnt = 1)
{
}
inline void decrease_counter(stat s, int64_t cnt)
{
}
inline void decrease_counter(stat s, size_t cnt)
{
}
inline void decrease_counter(stat s)
{
}
inline void track_minmax(stat s_min, stat s_max, int64_t value)
{
}
inline void track_max(stat s_max, stat s_value)
{
}
inline void track_avg(stat s_accu, stat s_cnt, int64_t value)
{
}
#endif

#if !defined(REMOTEHEAP_COLLECT_STATS) || defined(REMOTEHEAP_COLLECT_STATS_NO_TIMING)
class timing
{
  public:
	timing(stat s_time, stat s_cnt)
	{
	}
};

#define area_cs_stats
#define heap_cs_stats
#define heap_mapped_cs_stats

class event_timing
{
  public:
	event_timing(const char* name)
	{
	}
};
#endif
} // namespace remote_heap::stats

#ifdef NO_REMOTE_HEAP
#include <bit>
#include "Win32Heap.h"

//#	define TRACK_RH_ALLOCATIONS

struct sdata;
class LTAllocVA_no_remote_heap;

namespace remote_heap
{
class remote_heap_impl
{
  public:
	struct remote_ptr
	{
		operator bool() const
		{
			return !!sd;
		}
		bool operator!() const
		{
			return !sd;
		}
		operator const sdata*() const
		{
			return sd;
		}
		operator sdata*()
		{
			return sd;
		}
		remote_ptr& operator=(nullptr_t)
		{
			sd = nullptr;
			return *this;
		}

		sdata* sd = nullptr;
	};

	// lower fragmentation and have compatible interface with remote_heap
	[[nodiscard]] static constexpr size_t get_block_size(size_t size)
	{
		assert(size > 0);
		assert(size <= max_block_size);
		return std::max(std::bit_ceil(size), min_block_size);
	}
	[[nodiscard]] static size_t get_block_size(const sdata* ptr);
	[[nodiscard]] static size_t get_block_size(remote_ptr ptr)
	{
		assert(ptr);
		return get_block_size(ptr.sd);
	}

	[[nodiscard]] remote_ptr alloc(size_t size)
	{
		sdata* sd = (sdata*)heap.Alloc(get_block_size(size));
		assert(sd);
#ifdef TRACK_RH_ALLOCATIONS
		{
			std::lock_guard l(tracked_m);
			tracked[sd] = last_track_id++;
		}
#endif
		return remote_ptr{sd};
	}
	void dealloc(remote_ptr& ptr)
	{
		assert(ptr);
		if (ptr)
		{
#ifdef TRACK_RH_ALLOCATIONS
			{
				std::lock_guard l(tracked_m);
				assert(tracked.contains(ptr.sd));
				tracked.erase(ptr.sd);
			}
#endif
			heap.Free(ptr.sd);
			ptr.sd = nullptr;
		}
	}

	~remote_heap_impl()
	{
#ifdef TRACK_RH_ALLOCATIONS
		std::lock_guard l(tracked_m);
		assert(tracked.empty());
#endif
	}

#ifdef TRACK_RH_ALLOCATIONS
	std::mutex tracked_m;
	std::unordered_map<void*, uint> tracked;
	uint last_track_id{0};
#endif

  protected:
	Win32Heap heap{4 * 1048576, "remote_heap"};
	Win32Heap ltheap{4 * 1048576, "DefObj_heap"};

	friend class LTAllocVA_no_remote_heap;
};
} // namespace remote_heap
using remote_ptr = remote_heap::remote_heap_impl::remote_ptr;
extern remote_heap::remote_heap_impl* rheap;

#else
#include <functional>
#include <array>
#include "SpinCriticalSection.h"

#ifdef REMOTEHEAP_COLLECT_STATS
#include "Registry.h"
#include "RegKeys.h"
#endif
#include "Directories.h"

namespace remote_heap
{
static const inline size_t area_size = 2 * 1048576;
static const inline size_t max_empty_areas_count = 3; // reduce number of empty areas
static size_t max_lru_areas_count = 20;
static const inline size_t placeholders_count = max_lru_areas_count + 1;

static const inline size_t max_blocks_per_area = area_size / min_block_size;
static const inline uintptr_t max_blocks_mask = max_blocks_per_area - 1;
static const inline uintptr_t area_ptr_mask = ~max_blocks_mask;

#if defined(_DEBUG) || defined(VA_CPPUNIT)
extern bool fail_next_filemap;
#endif

using critical_section_t = CSpinCriticalSection;
class _critical_section_lock_t
{
  public:
	_critical_section_lock_t(CCriticalSection& cs, bool lock = true)
	    : cs(cs)
	{
		if (lock)
			this->lock();
	}
	virtual ~_critical_section_lock_t()
	{
		if (locked)
			unlock();
	}

	virtual void lock()
	{
		assert(!locked);
		cs.Lock();
		locked = true;
	}
	void unlock()
	{
		assert(locked);
		cs.Unlock();
		locked = false;
	}

  protected:
	CCriticalSection& cs;
	bool locked = false;
};
#if defined(REMOTEHEAP_COLLECT_STATS) && !defined(REMOTEHEAP_COLLECT_STATS_NO_TIMING)
class timing
{
  public:
	timing(stat s_time, stat s_cnt)
	    : s_time(s_time), s_cnt(s_cnt)
	{
		start_time = std::chrono::high_resolution_clock::now();
	}
	~timing()
	{
		auto stop_time = std::chrono::high_resolution_clock::now();
		auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(stop_time - start_time).count();
		track_avg(s_time, s_cnt, microseconds);
	}

  protected:
	const stat s_time, s_cnt;
	std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
};

#define area_cs_stats                                                                                                  \
	, ::remote_heap::stats::stat::timing_area_cs_us, ::remote_heap::stats::stat::timing_area_cs_cnt,                   \
	    ::remote_heap::stats::stat::area_cs_waiting, ::remote_heap::stats::stat::area_cs_waiting_max
#define heap_cs_stats                                                                                                  \
	, ::remote_heap::stats::stat::timing_heap_cs_us, ::remote_heap::stats::stat::timing_heap_cs_cnt,                   \
	    ::remote_heap::stats::stat::heap_cs_waiting, ::remote_heap::stats::stat::heap_cs_waiting_max
#define heap_mapped_cs_stats                                                                                           \
	, ::remote_heap::stats::stat::timing_heap_mapped_cs_us, ::remote_heap::stats::stat::timing_heap_mapped_cs_cnt,     \
	    ::remote_heap::stats::stat::heap_mapped_cs_waiting, ::remote_heap::stats::stat::heap_mapped_cs_waiting_max
namespace stats
{
class timed_critical_section_lock_t : public _critical_section_lock_t
{
  public:
	timed_critical_section_lock_t(CCriticalSection& cs, stat timing_cs_us, stat timing_cs_cnt, stat cs_waiting,
	                              stat cs_waiting_max, bool lock = true)
	    : _critical_section_lock_t(cs, false), timing_cs_us(timing_cs_us), timing_cs_cnt(timing_cs_cnt),
	      cs_waiting(cs_waiting), cs_waiting_max(cs_waiting_max)
	{
		if (lock)
			this->lock();
	}

	void lock() override
	{
		assert(!locked);
		if (::TryEnterCriticalSection(&cs.m_sect))
			increase_counter(timing_cs_cnt);
		else
		{
			increase_counter(cs_waiting);
			track_max(cs_waiting_max, cs_waiting);
			{
				remote_heap::stats::timing t(timing_cs_us, timing_cs_cnt);

				::EnterCriticalSection(&cs.m_sect);
			}
			decrease_counter(cs_waiting);
		}
		locked = true;
	}

  protected:
	const stat timing_cs_us;
	const stat timing_cs_cnt;
	const stat cs_waiting;
	const stat cs_waiting_max;
};
} // namespace stats
using critical_section_lock_t = stats::timed_critical_section_lock_t;
#else
using critical_section_lock_t = _critical_section_lock_t;
#endif
#if defined(REMOTEHEAP_COLLECT_STATS) && defined(REMOTEHEAP_TIMING_PROJLOAD_AND_FINDREF)
namespace stats
{
class event_timing
{
  public:
	event_timing(const char* name)
	    : name(name)
	{
		assert(name);
		start_time = std::chrono::high_resolution_clock::now();
	}
	~event_timing()
	{
		auto stop_time = std::chrono::high_resolution_clock::now();
		auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_time).count();

		char temp[4096];
		sprintf_s(temp,
		          "FEC:  *** %s ***\n"
		          "FEC: %dm%ds\n"
		          "FEC: DTypes alive (%d; %dkb)\n"
		          "FEC: Areas (%d)\n"
		          "FEC:     lru(%d) mapped(%d)\n"
		          "FEC: Mapped placeholders: total(%d) current(%d) max(%d)\n"
		          "FEC: Mapped classic: total(%d) current(%d) max(%d)\n",
		          name, int(milliseconds / 1000 / 60), int((milliseconds / 1000) % 60), (int)get_stat(stat::dtypes),
		          int(get_stat(stat::dtypes_bytes) / 1024), (int)get_stat(stat::areas),
		          (int)get_stat(stat::areas_pool_efficiency_lru),
		          (int)get_stat(stat::areas_pool_efficiency_need_to_map),
		          (int)get_stat(stat::areas_mapped_placeholder_total), (int)get_stat(stat::areas_mapped_placeholder),
		          (int)get_stat(stat::areas_mapped_placeholder_max), (int)get_stat(stat::areas_mapped_classic_total),
		          (int)get_stat(stat::areas_mapped_classic), (int)get_stat(stat::areas_mapped_classic_max));
		//		::MessageBoxA(nullptr, temp, "stats", MB_OK | MB_ICONEXCLAMATION);
		::OutputDebugStringA(temp);
	}

  protected:
	const char* name;
	std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
};
} // namespace stats
#endif
} // namespace remote_heap

namespace remote_heap
{

/*static bool EnableLockPagesPrivilege()
{
    CHandle token;
    if(!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token.m_h))
        return false;

    LUID luid;
    if(!::LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid))
        return false;

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if(!::AdjustTokenPrivileges(token, false, &tp, 0, nullptr, nullptr))
        return false;

    return ::GetLastError() == ERROR_SUCCESS;
}*/

// compile-time tags to verify locking
// function that needs locks should have a need_locks as a parameter
// caller should specify have_locks at that parameter
enum class lock_types : uint32_t
{
	none = 0,
	mapped = 1,
	area_list = 2,
	area = 4
};
constexpr lock_types operator|(lock_types left, lock_types right)
{
	return lock_types(uint32_t(left) | uint32_t(right));
}
template <lock_types lock_types_val = lock_types::none>
using have_locks = std::integral_constant<lock_types, lock_types_val>;

template <lock_types need_lock_types_val>
struct need_locks : public have_locks<need_lock_types_val>
{
	template <lock_types have_lock_types_val>
	need_locks(have_locks<have_lock_types_val>)
	{
		static_assert(
		    (need_lock_types_val == lock_types::none) ||
		        ((uint32_t(have_lock_types_val) & uint32_t(need_lock_types_val)) == uint32_t(need_lock_types_val)),
		    "Not enough locks!");
		static_assert((need_lock_types_val != lock_types::none) || (have_lock_types_val == lock_types::none),
		              "None locks wanted!");
	}
};

struct area_placeholder : public SLIST_ENTRY
{
	void* ph;
};

// implementation of heap
class remote_heap_impl
{
  public:
	remote_heap_impl()
	{
		for (size_t i = 0; i < areas.size(); i++)
			areas[i].size_index = i;

		::InitializeSListHead(&placeholders);
		if (placeholders_count > 0)
		{
			myVirtualAlloc2 = (VirtualAlloc2_t)(intptr_t)GetProcAddress(
			    GetModuleHandleA("kernelbase.dll"), "VirtualAlloc2");
			myMapViewOfFile3 = (MapViewOfFile3_t)(intptr_t)GetProcAddress(
			    GetModuleHandleA("kernelbase.dll"), "MapViewOfFile3");
			myUnmapViewOfFileEx = (UnmapViewOfFileEx_t)(intptr_t)GetProcAddress(
			    GetModuleHandleA("kernelbase.dll"), "UnmapViewOfFileEx");
			if (myVirtualAlloc2 && myMapViewOfFile3 && myUnmapViewOfFileEx)
			{
				for (size_t i = 0; i < placeholders_count; i++)
				{
					void* ph = myVirtualAlloc2(nullptr, nullptr, area_size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
					                           PAGE_NOACCESS, nullptr, 0);
					assert(ph);
					if (!ph)
						break;

					auto* ap =
					    (area_placeholder*)_aligned_malloc(sizeof(area_placeholder), MEMORY_ALLOCATION_ALIGNMENT);
					ap->ph = ph;
					::InterlockedPushEntrySList(&placeholders, ap);
				}
			}
		}

#ifdef REMOTEHEAP_COLLECT_STATS
#ifndef DEBUG
		bool enabled = !!::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "EnableRemoteHeapCollectStatsConsole", 0);
#else
		bool enabled = true;
#endif
		if (enabled)
			remote_heap::stats::ManageRemoteHeapStatsConsole(true);
#endif
	}

	~remote_heap_impl()
	{
		for (auto& a : areas)
		{
			assert(a.full.empty());
#ifdef REMOTEHEAP_SHOW_LEAKS
			dump_leaks(a);
			// Only few symbols related to the current view seems to leak. The number doesn't seem to increase over time
			// or by project size.
			assert(a.partial.empty());
#endif
			// no locks needed
			a.cleanup_empty_areas(have_locks<>{}, 0);
		}
		assert(areas_mapped_cnt == 0);
		cleanup_lru(have_locks<>{}, 0);
		assert(areas_lru_cnt == 0);
		assert(areas_mapped_lru.empty());

		while (auto* ap = (area_placeholder*)::InterlockedPopEntrySList(&placeholders))
		{
			::VirtualFree(ap->ph, 0, MEM_RELEASE);
			_aligned_free(ap);
		}

#ifdef REMOTEHEAP_COLLECT_STATS
		remote_heap::stats::ManageRemoteHeapStatsConsole(false);
#endif
	}

	class area;
	struct area_lists;

	// pointer to the block
	struct remote_ptr
	{
	  protected:
		uintptr_t p = 0;

	  public:
		operator bool() const
		{
			return !!p;
		}
		bool operator==(const remote_ptr& other) const
		{
			return p == other.p;
		}

		remote_ptr() = default;
		remote_ptr(nullptr_t)
		{
		}
		remote_ptr(area* a, size_t block)
		{
			assert(a);
			assert((uintptr_t(a) & a->block_mask) == 0);
			assert(block < a->total_blocks);
			p = uintptr_t(a) | block;
		}
		remote_ptr(const remote_ptr &) = default;

		remote_ptr& operator=(const remote_ptr& right)
		{
			p = right.p;
			return *this;
		}
		remote_ptr& operator=(nullptr_t)
		{
			p = 0;
			return *this;
		}

		[[nodiscard]] std::tuple<area*, size_t> deconstruct() const
		{
			return {(area*)(p & area_ptr_mask), p & max_blocks_mask};
		}

		[[nodiscard]] size_t _hash() const
		{
			return std::hash<uintptr_t>()(p);
		}

		struct hash
		{
			size_t operator()(remote_ptr p) const
			{
				return p._hash();
			}
		};
	};

	// class that holds information about a set (2Mb) of blocks
// area needs to be aligned on block_size as we'll use lsbits to point to the block_size within the area (and use only 4
// bytes in DType all together)
#pragma warning(disable : 4324)
	class alignas(max_blocks_per_area) area
	{
	  public:
		[[nodiscard]] static constexpr size_t get_areas_size_index(size_t block_size)
		{
			assert(std::bit_width(block_size) >= std::bit_width(min_block_size));
			return size_t(std::bit_width(block_size) - std::bit_width(min_block_size));
		}

		area(remote_heap_impl& heap, const size_t block_size)
		    : heap(heap), block_size(block_size), total_blocks(area_size / block_size),
#ifndef NDEBUG
		      block_mask(total_blocks - 1),
#endif
		      //			area_ptr_mask(~block_mask),
		      area_size_index(get_areas_size_index(block_size)), al(heap.areas[area_size_index]),
		      bitmap(total_blocks / sizeof(uint64_t) / 8, 0)
		{
			assert((uintptr_t(this) & block_mask) == 0);
			assert((block_size >= min_block_size) && (block_size <= max_block_size));
			assert(area_size_index < area_sizes);

			if (bitmap.empty())
			{
				// area is less than 64 blocks
				assert(total_blocks < 64);
				bitmap.emplace_back(uint64_t(-1) << total_blocks);
			}

			static bool no_system_resoures = false;
			{
				stats::timing t(stats::stat::timing_CreateFileMapping_us, stats::stat::timing_CreateFileMapping_cnt);

#ifndef VA_CPPUNIT
				static const bool enabled_heap_on_disk = !!::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, "EnableDebugRemoteHeapOnDisk", 0);
#else
				static const bool enabled_heap_on_disk = false;
#endif
				HANDLE filehandle2 = INVALID_HANDLE_VALUE;
				if (enabled_heap_on_disk)
				{
					wchar_t filename[64];
					swprintf_s(filename, L"vaheap_%lx_%zu_%p.area", ::GetCurrentProcessId(), area_size_index, this);
					filehandle2 = ::CreateFileW(VaDirs::GetDbDir() + L"\\" + filename, GENERIC_READ | GENERIC_WRITE, 0,
					                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				}

				if (!no_system_resoures && are_large_pages_enabled() && !enabled_heap_on_disk)
				{
					filemap.Attach(::CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
					                                   PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES, 0, area_size,
					                                   nullptr));
					assert(filemap || (::GetLastError() == ERROR_NO_SYSTEM_RESOURCES));
					if (!filemap)
						no_system_resoures = ::GetLastError() == ERROR_NO_SYSTEM_RESOURCES;
					else
						largepage = true;
				}
				if (!filemap)
					filemap.Attach(
					    ::CreateFileMapping(filehandle2, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, area_size, nullptr));
				assert(filemap);

				if (filehandle2 && (filehandle2 != INVALID_HANDLE_VALUE))
					this->filehandle.Attach(filehandle2);
			}

			stats::increase_counter(stats::stat::areas);
			stats::increase_counter(largepage, stats::stat::areas_largefilemap);
			stats::increase_counter(stats::stat::blocks_available, total_blocks);
			stats::track_max(stats::stat::areas_max, stats::stat::areas);
		}
		~area()
		{
			critical_section_lock_t area_lck(cs area_cs_stats);
			assert(empty());
			assert(mapped_cnt == 0);
			assert(!mapped);

			stats::decrease_counter(stats::stat::areas);
			stats::decrease_counter(largepage, stats::stat::areas_largefilemap);
			stats::decrease_counter(stats::stat::blocks_available, total_blocks);
		}

		[[nodiscard]] bool full() const
		{
			check_blocks_counters();
			return !free_blocks;
		}
		[[nodiscard]] bool empty() const
		{
			check_blocks_counters();
			return !used_blocks;
		}
		[[nodiscard]] bool will_be_empty_after_dealloc() const
		{
			check_blocks_counters();
			return used_blocks == 1;
		}
		[[nodiscard]] bool has_free() const
		{
			check_blocks_counters();
			return free_blocks > 0;
		}
		[[nodiscard]] size_t get_free() const
		{
			check_blocks_counters();
			return free_blocks;
		}
		[[nodiscard]] size_t get_used() const
		{
			check_blocks_counters();
			return used_blocks;
		}
#ifdef REMOTEHEAP_SHOW_LEAKS
		[[nodiscard]] const std::vector<uint64_t>& get_bitmap() const
		{
			return bitmap;
		}
#endif

		[[nodiscard]] remote_ptr alloc(need_locks<lock_types::area>)
		{
			// access under area lock

			check_blocks_counters();
			assert(filemap);
			if (!filemap)
				return {};
			assert(free_blocks > 0);
			if (!free_blocks)
				return {};

			for (size_t i = 0; i < bitmap.size(); i++, last_index = (last_index + 1) % bitmap.size())
			{
				stats::increase_counter(stats::stat::blocks_total_allocation_iterations);

				uint64_t bmp = ~bitmap[last_index]; // in bmp, 1s are free
				if (!bmp)
					continue; // nothing is free

				unsigned long bit = 0;
#ifdef _WIN64
				_BitScanForward64(&bit, bmp);
#else
				if (!_BitScanForward(&bit, (uint32_t)bmp) && _BitScanForward(&bit, (uint32_t)(bmp >> 32)))
					bit += 32;
#endif

				bitmap[last_index] |= 1ull << bit;
				++used_blocks;
				--free_blocks;
				size_t block = sizeof(bmp) * 8 * last_index + bit;
				assert(block < total_blocks);

				stats::increase_counter(stats::stat::blocks);
				stats::increase_counter(stats::stat::blocks_total_allocations);
				stats::increase_counter(stats::stat::blocks_bytes, block_size);
				stats::track_max(stats::stat::blocks_max, stats::stat::blocks);
				return {this, block};
			}

			assert(!"This shouldn't happen! Something had to be free!");
			return {};
		}

		[[nodiscard]] bool is_block_free(size_t block, need_locks<lock_types::area>) const
		{
			// used for debugging
			size_t bit = block % (sizeof(bitmap[0]) * 8);
			const uint64_t& bitmap_ref = bitmap[block / sizeof(bitmap[0]) / 8];
			return !(bitmap_ref & (1ull << bit));
		}

		[[nodiscard]] bool dealloc(size_t block, need_locks<lock_types::area>)
		{
			// access under area lock

			assert(block < total_blocks);
#ifdef REMOTEHEAP_TRACK_MAPPED_BLOCKS
			{
				_critical_section_lock_t mplck(mapped_pointers_cs);
				auto track_it = mapped_pointers.find(remote_ptr{this, block});
				//				if(track_it != mapped_pointers.end())
				//				{
				//					char temp[512];
				//					sprintf_s(temp, "FEC: [%d] %p:%d dealloc check %d\n", ::GetCurrentThreadId(), this,
				// block, track_it->second);
				//					::OutputDebugStringA(temp);
				//				}
				assert((track_it == mapped_pointers.end()) || !track_it->second);
			}
#endif

			size_t bit = block % (sizeof(bitmap[0]) * 8);
			uint64_t& bitmap_ref = bitmap[block / sizeof(bitmap[0]) / 8];
			bool is_allocated = !!(bitmap_ref & (1ull << bit));
			assert(is_allocated);
			if (!is_allocated)
				return false; // not a valid situation, but handle as benign as possible
			bitmap_ref &= ~(1ull << bit);

			assert(used_blocks > 0);
			--used_blocks;
			++free_blocks;
			stats::decrease_counter(stats::stat::blocks);
			stats::decrease_counter(stats::stat::blocks_bytes, block_size);
			return true;
		}

		[[nodiscard]] void* map(need_locks<lock_types::area>, area_placeholder** placeholder = nullptr)
		{
			// access under area lock

			assert(filemap);
			if (!mapped && filemap)
			{
#if defined(_DEBUG) || defined(VA_CPPUNIT)
				if (fail_next_filemap)
					return nullptr;
#endif

				{
					stats::timing t(stats::stat::timing_MapViewOfFile_us, stats::stat::timing_MapViewOfFile_cnt);

					area_placeholder* ap = placeholder ? *placeholder : nullptr;
					if (!ap && heap.myMapViewOfFile3)
						ap = (area_placeholder*)::InterlockedPopEntrySList(&heap.placeholders);

					if (ap)
					{
						assert(ap->ph);
						mapped = heap.myMapViewOfFile3(filemap, nullptr, ap->ph, 0, area_size, MEM_REPLACE_PLACEHOLDER,
						                               PAGE_READWRITE, nullptr, 0);
						assert(mapped == ap->ph);
						if (!mapped)
							::InterlockedPushEntrySList(&heap.placeholders, ap);
						else
						{
							mapped_using_placeholder = ap;

							stats::increase_counter(stats::stat::areas_mapped_placeholder_total);
							stats::increase_counter(stats::stat::areas_mapped_placeholder);
							stats::track_max(stats::stat::areas_mapped_placeholder_max,
							                 stats::stat::areas_mapped_placeholder);
						}
					}
					if (!ap || !mapped)
					{
						mapped = (void*)::MapViewOfFile(
						    filemap, FILE_MAP_ALL_ACCESS | (largepage ? FILE_MAP_LARGE_PAGES : 0u), 0, 0, 0);

						stats::increase_counter(stats::stat::areas_mapped_classic_total);
						stats::increase_counter(stats::stat::areas_mapped_classic);
						stats::track_max(stats::stat::areas_mapped_classic_max, stats::stat::areas_mapped_classic);
					}
					assert(mapped);
				}

				stats::increase_counter(stats::stat::areas_mapped);
				stats::track_max(stats::stat::areas_mapped_max, stats::stat::areas_mapped);
			}
			else
			{
				assert(!placeholder || !*placeholder);
				if (placeholder && *placeholder)
					::InterlockedPushEntrySList(&heap.placeholders, *placeholder);
			}

			if (placeholder)
				*placeholder = nullptr;
			return mapped;
		}
		void unmap_asserts(need_locks<lock_types::mapped>) const;
		void unmap(need_locks<lock_types::area> nl, area_placeholder** steal_placeholder = nullptr)
		{
			// note: unmap needs to be preceded by remove_from_lru (with mapped lock)
			// access under area lock

			assert(mapped_cnt == 0);
			//			unmap_asserts(nl);			// don't wanna lock just for assert checks
			//			assert(mapped);				// we treat failed mappings as ok to maintain counters values
			if (!mapped)
				return;

			{
				stats::timing t(stats::stat::timing_UnmapViewOfFile_us, stats::stat::timing_UnmapViewOfFile_cnt);

				if (mapped_using_placeholder)
				{
					assert(mapped == mapped_using_placeholder->ph);
					// for some reason UnmapViewOfFile2 fails
					auto ret = heap.myUnmapViewOfFileEx(mapped, MEM_PRESERVE_PLACEHOLDER);
					(void)ret;
					assert(!!ret);
					if (steal_placeholder)
						*steal_placeholder = mapped_using_placeholder;
					else
						::InterlockedPushEntrySList(&heap.placeholders, mapped_using_placeholder);
					mapped_using_placeholder = nullptr;

					stats::decrease_counter(stats::stat::areas_mapped_placeholder);
				}
				else
				{
					::UnmapViewOfFile(mapped);
					stats::decrease_counter(stats::stat::areas_mapped_classic);
				}
				mapped = nullptr;
			}

			assert(heap.areas_lru_unmap_pending_cnt > 0);
			--heap.areas_lru_unmap_pending_cnt;

			stats::decrease_counter(stats::stat::areas_lru_unmap_pending);
			stats::decrease_counter(stats::stat::areas_mapped);
		}

		[[nodiscard]] bool is_lru() const
		{
			return mapped_cnt == 0;
		}

		remote_heap_impl& heap;

		using list_iterator_t = std::list<area*>::iterator;

		// access under mapped+area locks (lock in that order)
		list_iterator_t areas_full_it;
		list_iterator_t areas_partial_it;
		//	list_iterator_t areas_empty_it;

		// access under area_list+area locks (lock in that order)
		list_iterator_t areas_mapped_lru_it;
		uint32_t mapped_cnt = 0; // if 0, areas is LRU cache

		critical_section_t cs;

	  protected:
		[[nodiscard]] static bool are_large_pages_enabled()
		{
			//		static bool ok = (::GetLargePageMinimum() == area_size) && EnableLockPagesPrivilege();
			//		return ok;
			return false; // seems its number is quite limited; don't want to cause trouble
		}
		void check_blocks_counters() const
		{
			assert((used_blocks + free_blocks) == total_blocks);
		}

	  public:
		const size_t block_size;
		const size_t total_blocks;
#ifndef NDEBUG
		const uintptr_t block_mask;
#endif
		//		const uintptr_t area_ptr_mask;
		const size_t area_size_index;
		area_lists& al;

	  protected:
		size_t used_blocks = 0;
		size_t free_blocks = total_blocks;

		size_t last_index = 0; // to speed up search

		std::vector<uint64_t> bitmap; // 1 is used block

		CHandle filehandle; // used optionally, for debugging
		CHandle filemap;
		bool largepage = false;
		void* mapped = nullptr;
		area_placeholder* mapped_using_placeholder = nullptr;

#ifdef REMOTEHEAP_TRACK_MAPPED_BLOCKS
	  public:
		critical_section_t mapped_pointers_cs;
		std::unordered_map<remote_ptr, size_t, remote_ptr::hash> mapped_pointers;
#endif

#ifdef REMOTEHEAP_COLLECT_STATS
		friend void remote_heap::stats::ManageRemoteHeapStatsConsole(bool open);
#endif
	};

	// class that restricts access to a single block within the mapped area; it handles area mapping
	class mapped_block
	{
		area* a = nullptr;
		size_t block = 0;
		char* block_ptr = nullptr;

	  public:
		mapped_block() = default;
		mapped_block(mapped_block&& other)
		{
			std::swap(a, other.a);
			std::swap(block, other.block);
			std::swap(block_ptr, other.block_ptr);
		}
		mapped_block(area* a, size_t block)
		    : mapped_block(remote_ptr{a, block})
		{
		}
		mapped_block(remote_ptr ptr)
		{
			remote_heap::stats::timing t(remote_heap::stats::stat::timing_mapped_block_constructor_us,
			                             remote_heap::stats::stat::timing_mapped_block_constructor_cnt);

			assert(ptr);
			std::tie(a, block) = ptr.deconstruct();
			assert(a);
			assert(block < a->total_blocks);
			if (!a)
				return;

			critical_section_lock_t mapped_lck(a->heap.mapped_cs heap_mapped_cs_stats);
			critical_section_lock_t area_lck(a->cs area_cs_stats);
			bool cleanup_lru = false;
			std::list<area*> lru_areas_to_unmap;

			if (a->areas_mapped_lru_it != a->heap.areas_mapped_lru.end())
			{
				if (a->mapped_cnt > 0)
				{
					// already mapped
					stats::increase_counter(stats::stat::areas_pool_efficiency_alread_mapped);
				}
				else
				{
					// mapped, but unused (as lru cache)
					++a->heap.areas_mapped_cnt;
					--a->heap.areas_lru_cnt;
					a->heap.check_areas_counters(have_locks<lock_types::mapped | lock_types::area>{});

					stats::increase_counter(stats::stat::areas_pool_mapped);
					stats::decrease_counter(stats::stat::areas_pool_lru);
					stats::increase_counter(stats::stat::areas_pool_efficiency_lru);
				}
			}
			else
			{
				// need to map
				assert(a->mapped_cnt == 0);
				a->heap.areas_mapped_lru.push_front(a);
				++a->heap.areas_mapped_cnt;
				a->heap.check_areas_counters(have_locks<lock_types::mapped | lock_types::area>{});
				a->areas_mapped_lru_it = a->heap.areas_mapped_lru.begin();

				// likely often true
				// cleanup_lru() will hold mapped + lru areas count at max_lru_areas_count.
				// The most efficient address space usage is when the number of placeholders is equal to
				// max_lru_areas_count. In practice, we don't have area lock here (for area that is going to be
				// expunged), so we will first do our map and then cleanup_lru later. This will lead to using more
				// placeholders than needed, max_lru_areas_count + 1 for one thread; possibly more when multithreaded.
				// To try to avoid that, cleanup_lru_condition will check if it can immediately remove one lru by going
				// through older half of lrus and see if lock for one of those could be obtained immediately; if
				// unsuccessful, just do it later.
				cleanup_lru = a->heap.cleanup_lru_condition__try_now(
				                  have_locks<lock_types::mapped | lock_types::area>{}, lru_areas_to_unmap) >
				              0; // avoid locking later if there is no need for lru cleanup

				stats::increase_counter(stats::stat::areas_pool_mapped);
				stats::increase_counter(stats::stat::areas_pool_efficiency_need_to_map);
			}

			mapped_lck.unlock();
			area_placeholder* placeholder = nullptr;
			for (area* a2 : lru_areas_to_unmap)
			{
				a2->unmap(have_locks<lock_types::area>{}, placeholder ? nullptr : &placeholder);
				a2->cs.Unlock();
			}
			++a->mapped_cnt;
			void* blocks;
			do
			{
				blocks = a->map(have_locks<lock_types::area>{}, &placeholder);
				if (!blocks)
				{
#if defined(_DEBUG) || defined(VA_CPPUNIT)
					if (!fail_next_filemap)
#endif
						assert(!placeholder); // map should be successful if we already had placeholder
					area_lck.unlock();
					a->heap.cleanup_lru(have_locks<>{},
					                    max_lru_areas_count /=
					                    2); // critical address space situation; give up from using LRU cache gradually
					if (!max_lru_areas_count)
						mapped_lck.lock(); // we are done trying and going out of the loop; we need mapped lock below,
						                   // so lock before area
					area_lck.lock();
				}
			} while (!blocks && (max_lru_areas_count > 0));
#if defined(_DEBUG) || defined(VA_CPPUNIT)
			if (!fail_next_filemap)
#endif
				assert(blocks);
			if (!blocks)
			{
				//				// undo changes -> will be handled by desctructor instead
				//				--a->mapped_cnt;
				a->heap.check_areas_counters(have_locks<lock_types::mapped | lock_types::area>{});

				//				a = nullptr;
				block = 0;
				block_ptr = nullptr;
				return;
			}

			block_ptr = ((char*)blocks) + block * a->block_size;
#ifdef REMOTEHEAP_TRACK_MAPPED_BLOCKS
			{
				_critical_section_lock_t mplck(a->mapped_pointers_cs);
				//				char temp[512];
				//				sprintf_s(temp, "FEC: [%d] %p:%d mapped_block %d->%d\n", ::GetCurrentThreadId(), a,
				// block, a->mapped_pointers[remote_ptr{a, block}], a->mapped_pointers[remote_ptr{a, block}] + 1);
				//				::OutputDebugStringA(temp);
				++a->mapped_pointers[remote_ptr{a, block}];
			}
#endif

			area_lck.unlock();
			if (cleanup_lru)
				a->heap.cleanup_lru(have_locks<>{});

			stats::increase_counter(stats::stat::blocks_mapped);
			stats::track_max(stats::stat::blocks_mapped_max, stats::stat::blocks_mapped);
		}
		~mapped_block()
		{
			if (!a)
				return;

			remote_heap::stats::timing t(remote_heap::stats::stat::timing_mapped_block_destructor_us,
			                             remote_heap::stats::stat::timing_mapped_block_destructor_cnt);

			bool cleanup_lru = false;
			{
				critical_section_lock_t mapped_lck(a->heap.mapped_cs heap_mapped_cs_stats);
				critical_section_lock_t area_lck(a->cs area_cs_stats);

				assert(a->areas_mapped_lru_it != a->heap.areas_mapped_lru.end());
				if (a->areas_mapped_lru_it == a->heap.areas_mapped_lru.end())
					return;
				assert(a->mapped_cnt > 0);

#ifdef REMOTEHEAP_TRACK_MAPPED_BLOCKS
				if (block_ptr)
				{
					_critical_section_lock_t mplck(a->mapped_pointers_cs);
					auto track_it = a->mapped_pointers.find(remote_ptr{a, block});
					assert(track_it != a->mapped_pointers.end());
					//					char temp[512];
					//					sprintf_s(temp, "FEC: [%d] %p:%d ~mapped_block %d->%d\n",
					//::GetCurrentThreadId(), a, block, track_it->second, track_it->second - 1);
					//					::OutputDebugStringA(temp);
					if (!--track_it->second)
						a->mapped_pointers.erase(track_it);
				}
#endif

				if (--a->mapped_cnt == 0)
				{
					// put to the lru list front
					if (a->areas_mapped_lru_it != a->heap.areas_mapped_lru.begin())
						a->heap.areas_mapped_lru.splice(a->heap.areas_mapped_lru.begin(), a->heap.areas_mapped_lru,
						                                a->areas_mapped_lru_it); // put to front

					--a->heap.areas_mapped_cnt;
					++a->heap.areas_lru_cnt;
					a->heap.check_areas_counters(have_locks<lock_types::mapped | lock_types::area>{});

					// likely rarely true
					cleanup_lru = !!a->heap.cleanup_lru_condition(
					    have_locks<lock_types::mapped | lock_types::area>{}); // avoid locking later if there is no need
					                                                          // for lru cleanup

					stats::increase_counter(stats::stat::areas_pool_lru);
					stats::decrease_counter(stats::stat::areas_pool_mapped);
				}
			}

			if (cleanup_lru)
				a->heap.cleanup_lru(have_locks<>{});

			stats::decrease_counter(stats::stat::blocks_mapped);
		}

		void get(void* dest, size_t offset, size_t size)
		{
			assert(a);
			assert((offset + size) <= a->block_size);
			assert(dest);
			assert(block_ptr);
			if (!block_ptr)
				return;
			memcpy_s(dest, size, &block_ptr[offset], size);

			stats::increase_counter(stats::stat::blocks_reads);
			stats::increase_counter(stats::stat::blocks_read_bytes, size);
		}
		void set(const void* src, size_t offset, size_t size)
		{
			assert(a);
			assert((offset + size) <= a->block_size);
			assert(src);
			assert(block_ptr);
			if (!block_ptr)
				return;
			memcpy_s(&block_ptr[offset], a->block_size, src, size);

			stats::increase_counter(stats::stat::blocks_writes);
			stats::increase_counter(stats::stat::blocks_write_bytes, size);
		}

		template <typename TYPE>
		[[nodiscard]] TYPE get_value(size_t offset = 0)
		{
			TYPE ret;
			get(&ret, offset, sizeof(ret));
			return ret;
		}
		template <typename TYPE>
		void set_value(TYPE value, size_t offset = 0)
		{
			set(&value, offset, sizeof(value));
		}

		[[nodiscard]] void* get_block_ptr()
		{
			return block_ptr;
		}
	};

	[[nodiscard]] mapped_block map(remote_ptr ptr)
	{
		assert(ptr);
		return mapped_block(ptr);
	}

	// a class that holds lists of areas with the same block size
	struct area_lists
	{
		void cleanup_empty_areas(need_locks<lock_types::none>, size_t empty_areas_count = max_empty_areas_count)
		{
			// no locks are wanted for this call
			critical_section_lock_t arealist_lck(cs heap_cs_stats);
			//			if(size_index >= 4)
			//				empty_areas_count = 0;
			while (empty.size() > empty_areas_count)
			{
				delete empty.front(); // will be area-locked in destructor since unmap (with area lock, without arealist
				                      // lock) might be in progress; cannot lock here as the lock will be destructed
				empty.pop_front();

				stats::decrease_counter(stats::stat::areas_pool_empty_x + size_index);
			}
		}

		size_t size_index = 0;
		critical_section_t cs;    // area list lock
		std::list<area*> full;    // areas with no free space
		std::list<area*> partial; // areas with some free space
		std::list<area*> empty;   // areas completely empty
	};

	[[nodiscard]] static size_t upper_power_of_two(size_t value)
	{
		unsigned long index = 0;
#ifndef _WIN64
		_BitScanReverse(&index, --value);
#else
		_BitScanReverse64(&index, --value);
#endif
		if (value == 0)
			index = (unsigned long)-1;
		return (size_t(1u) << (index + 1));
	}
	[[nodiscard]] static size_t get_block_size(size_t size)
	{
		return std::max(upper_power_of_two(size), min_block_size);
	}
	[[nodiscard]] remote_ptr alloc(size_t size)
	{
		assert((size > 0) && (size <= max_block_size));
		size = get_block_size(size);
		auto size_index = area::get_areas_size_index(size);
		auto& al = areas[size_index];
		const size_t block_size = min_block_size << size_index;

		critical_section_lock_t arealist_lck(al.cs heap_cs_stats);

		// get free area
		if (al.partial.empty())
		{
			if (al.empty.empty())
			{
				auto* a = new area(*this, block_size); // do under arealist lock since it's rare enough
				al.empty.push_front(a);
				a->areas_full_it = al.full.end();
				a->areas_partial_it = al.partial.end();
				a->areas_mapped_lru_it = areas_mapped_lru.end();
			}
			else
				stats::decrease_counter(stats::stat::areas_pool_empty_x + size_index);

			al.partial.splice(al.partial.cbegin(), al.empty, al.empty.cbegin());
			assert(al.partial.front()->areas_partial_it == al.partial.end());
			al.partial.front()->areas_partial_it = al.partial.begin();

			stats::increase_counter(stats::stat::areas_pool_partial_x + size_index);
			stats::increase_counter(stats::stat::areas_pool_empty_to_partial_transitions);
		}

		area* a = al.partial.front();
		critical_section_lock_t area_lck(a->cs area_cs_stats);

		assert(a->has_free());
		if (a->get_free() == 1)
		{
			// will be full after alloc(), so we'll move area to full while we have lock
			assert(a == al.partial.front());
			assert(a->areas_full_it == al.full.end());
			al.full.splice(al.full.cbegin(), al.partial, al.partial.cbegin());
			a->areas_full_it = al.full.begin();
			a->areas_partial_it = al.partial.end();

			stats::increase_counter(stats::stat::areas_pool_full_x + size_index);
			stats::decrease_counter(stats::stat::areas_pool_partial_x + size_index);
			stats::increase_counter(stats::stat::areas_pool_partial_to_full_transitions);
		}
		arealist_lck.unlock();

		remote_ptr p = a->alloc(have_locks<lock_types::area>{});
		assert(p);
		return p;
	}
	void dealloc(remote_ptr ptr, bool do_mapped_lock = false)
	{
		if (!ptr)
			return;

		auto [a, block] = ptr.deconstruct();
		assert(a);

		std::optional<critical_section_lock_t> mapped_lck;
		if (do_mapped_lock)
			mapped_lck.emplace(a->heap.mapped_cs heap_mapped_cs_stats);
		critical_section_lock_t arealist_lck(a->al.cs heap_cs_stats);
		critical_section_lock_t area_lck(a->cs area_cs_stats);
		if (!do_mapped_lock && a->will_be_empty_after_dealloc())
		{
			// Mapped lock is needed only if area becomes empty, which is rare, so it makes sense
			// to repeat arealist and area locks instead of obtaining mapped lock every time on dealloc. If on the
			// repeated call will_be_empty_after_dealloc() becomes false, it won't matter.
			stats::increase_counter(stats::stat::dealloc_lock_retries);
			area_lck.unlock();
			arealist_lck.unlock();
			return dealloc(ptr, true);
		}

		// under arealist lock to enforce lock order; quick operation
		if (!a->dealloc(block, have_locks<lock_types::area_list | lock_types::area>{}))
			return; // shouldn't happen if everything is ok

		bool unmap = false;
		bool cleanup_empty_areas = false;
		if (a->areas_full_it != a->al.full.end())
		{
			a->al.partial.splice(a->al.partial.cbegin(), a->al.full, a->areas_full_it);
			a->areas_full_it = a->al.full.end();
			a->areas_partial_it = a->al.partial.begin();

			stats::increase_counter(stats::stat::areas_pool_partial_x + a->area_size_index);
			stats::decrease_counter(stats::stat::areas_pool_full_x + a->area_size_index);
			stats::increase_counter(stats::stat::areas_pool_full_to_partial_transitions);
		}
		else if (a->empty())
		{
			assert(mapped_lck);
			assert(a->mapped_cnt == 0);
			if (a->areas_mapped_lru_it != areas_mapped_lru.end())
			{
				remove_from_lru(have_locks<lock_types::mapped | lock_types::area_list | lock_types::area>{}, a);
				unmap = true;
			}

			assert(a->areas_partial_it != a->al.partial.end());
			a->al.empty.splice(a->al.empty.cbegin(), a->al.partial, a->areas_partial_it);
			a->areas_partial_it = a->al.partial.end();
			cleanup_empty_areas = true;

			stats::increase_counter(stats::stat::areas_pool_empty_x + a->area_size_index);
			stats::decrease_counter(stats::stat::areas_pool_partial_x + a->area_size_index);
			stats::increase_counter(stats::stat::areas_pool_partial_to_empty_transitions);
		}

		mapped_lck.reset();
		arealist_lck.unlock();
		if (unmap)
			a->unmap(have_locks<lock_types::area>{});
		area_lck.unlock();
		if (cleanup_empty_areas)
			a->al.cleanup_empty_areas(have_locks<>{}, max_empty_areas_count);
	}

	void remove_from_lru(need_locks<lock_types::mapped | lock_types::area> nl, area* a)
	{
		// note: remove_from_lru needs to be followed by unmap (without mapped lock)
		// both mapped+area locks needed
		assert(a->mapped_cnt == 0);
		assert(a->areas_mapped_lru_it != areas_mapped_lru.end());
		areas_mapped_lru.erase(a->areas_mapped_lru_it);
		a->areas_mapped_lru_it = areas_mapped_lru.end();
		--a->heap.areas_lru_cnt;
		++a->heap.areas_lru_unmap_pending_cnt;
		a->heap.check_areas_counters(nl);

		stats::increase_counter(stats::stat::areas_lru_unmap_pending);
		stats::track_max(stats::stat::areas_lru_unmap_pending_max, stats::stat::areas_lru_unmap_pending);
		stats::decrease_counter(stats::stat::areas_pool_lru);
		stats::increase_counter(stats::stat::areas_pool_expunged_from_lru);
	}
	// returns how many lrus need to be cleaned up
	[[nodiscard]] size_t cleanup_lru_condition(need_locks<lock_types::mapped>,
	                                           size_t lru_areas_count = max_lru_areas_count) const
	{
		// mapped lock wanted
		//		// mapped areas are included in cleanup
		//		return (size_t)std::min(
		//			(intptr_t)areas_lru_cnt,
		//			std::max(0,
		//					 intptr_t(areas_mapped_cnt + areas_lru_cnt /*+ areas_lru_unmap_pending_cnt*/) -
		//(intptr_t)lru_areas_count
		//			)
		//		);
		return (size_t)std::max(intptr_t(0), intptr_t(areas_lru_cnt) - intptr_t(lru_areas_count));
	}
	void cleanup_lru(need_locks<lock_types::none>, size_t lru_areas_count = max_lru_areas_count)
	{
		// no locks are wanted for this call
		critical_section_lock_t mapped_lck(mapped_cs heap_mapped_cs_stats);
		while (cleanup_lru_condition(have_locks<lock_types::mapped>{}, lru_areas_count) > 0)
		{
			{
				// as we unlock heap_mapped lock, in every iteration the last lru area needs to be found again
				// sounds terribly inefficient, but in all normal cases, only one lru area is expunged at a time
				auto it = std::find_if(areas_mapped_lru.rbegin(), areas_mapped_lru.rend(),
				                       [](area* a) { return a->is_lru(); });
				assert(it != areas_mapped_lru.rend());
				if (it == areas_mapped_lru.rend())
					break; // just in case
				area* a = *it;

				critical_section_lock_t area_lck(a->cs area_cs_stats);
				if (!a->is_lru())
					continue; // changed while obtaining the lock
				remove_from_lru(have_locks<lock_types::mapped | lock_types::area>{}, a);
				mapped_lck.unlock();
				a->unmap(have_locks<lock_types::area>{});
			}
			mapped_lck.lock();
		}
	}
	[[nodiscard]] size_t cleanup_lru_condition__try_now(need_locks<lock_types::mapped> locks,
	                                                    std::list<area*>& lru_areas_to_unmap,
	                                                    size_t lru_areas_count = max_lru_areas_count)
	{
		// description in mapped_block::mapped_block
		if (areas_mapped_lru.empty())
			return 0;
		size_t cleanup_needed = cleanup_lru_condition(locks, lru_areas_count);

		size_t max_lrus_to_check =
		    1 /*areas_lru_cnt / 2*/; // don't flush most recent ones; just check the older half (later changed to max 1
		                             // as it flushed too recent LRUs and caused slowdowns)
		auto it = std::prev(areas_mapped_lru.end());
		while ((cleanup_needed > 0) && (it != areas_mapped_lru.end()) && (max_lrus_to_check > 0))
		{
			area* a = *it;
			it =
			    (it != areas_mapped_lru.begin())
			        ? std::prev(it)
			        : areas_mapped_lru
			              .end(); // would be simpler to use reverse_iterator, but it internally holds iterator previous
			                      // to the one it points to, which is the iterator we delete, so it cannot be used
			if (!a->is_lru())
				continue;

			if (::TryEnterCriticalSection(&a->cs.m_sect))
			{
				if (!a->is_lru())
					continue; // changed while obtaining the lock
				remove_from_lru(have_locks<lock_types::mapped | lock_types::area>{}, a);
				lru_areas_to_unmap.emplace_back(a);
				--cleanup_needed;
			}
			--max_lrus_to_check;
		}
		return cleanup_needed;
	}

	void check_areas_counters(need_locks<lock_types::mapped>)
	{
		// mapped lock needed
		size_t x = areas_mapped_cnt + areas_lru_cnt;
		size_t y = areas_mapped_lru.size();
		assert("check_areas_counters() failed" && (x == y));
	}

  protected:
#ifdef REMOTEHEAP_SHOW_LEAKS
	void dump_leaks(const area_lists& al);
#endif

	// note: first lock mapped lock, then area_list and then area lock!
	// area_list and mapped locks should be quick

	std::array<area_lists, area_sizes> areas;

	// access under mapped lock
	critical_section_t mapped_cs;
	std::list<area*> areas_mapped_lru; // areas currently mapped and used or lru cached (lru at front)
	size_t areas_mapped_cnt = 0;
	size_t areas_lru_cnt = 0;
	std::atomic<size_t> areas_lru_unmap_pending_cnt = 0; // areas removed from lru, but still not unmapped

	// placeholders lock may be obtained anytime
	__declspec(align(MEMORY_ALLOCATION_ALIGNMENT)) SLIST_HEADER placeholders;

	using VirtualAlloc2_t = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
	using MapViewOfFile3_t = PVOID(WINAPI*)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG,
	                                        MEM_EXTENDED_PARAMETER*, ULONG);
	using UnmapViewOfFileEx_t = BOOL(WINAPI*)(PVOID, ULONG);
	VirtualAlloc2_t myVirtualAlloc2 = nullptr;
	MapViewOfFile3_t myMapViewOfFile3 = nullptr;
	UnmapViewOfFileEx_t myUnmapViewOfFileEx = nullptr;

#ifdef REMOTEHEAP_COLLECT_STATS
	friend void remote_heap::stats::ManageRemoteHeapStatsConsole(bool open);
#endif
};

inline void remote_heap_impl::area::unmap_asserts(need_locks<lock_types::mapped>) const
{
	// mapped lock needed
	assert(areas_mapped_lru_it == heap.areas_mapped_lru.end());
}

} // namespace remote_heap

using remote_ptr = remote_heap::remote_heap_impl::remote_ptr;
using mapped_block = remote_heap::remote_heap_impl::mapped_block;
extern remote_heap::remote_heap_impl* rheap;

#endif // !NO_REMOTE_HEAP
