#ifndef _VA_HASHTABLE_H_
#define _VA_HASHTABLE_H_

#include <vector>
#include "foo.h"
#include "DBLock.h"
//#include "ltalloc/ltalloc.hpp"
#include "ltalloc/ltalloc_va.h"
#include <map>
#include <shared_mutex>
#include "parallel_hashmap/phmap.h"

// No virtual methods -- prevent need of vtableptr to reduce memory required.
class DefObj : public DType
{
	friend class VAHashTable;
	// DefObjs are created, owned and destroyed by VAHashTable - it sets next after construction
	DefObj(DType&& idxStruct) : DType(std::move(idxStruct))
	{
		increase_counter(remote_heap::stats::stat::defobjs);
	}
#ifdef REMOTEHEAP_COLLECT_STATS
	~DefObj()
	{
		// warning: non-virtual destructor!
		decrease_counter(remote_heap::stats::stat::defobjs);
	}
#endif

  public:
// note: pointers are declared as const to be sure no one fiddles with them; when changed in VAHashTable, const_cast is used
	DefObj*const next = nullptr;		// symbol hash list
	DefObj*const next_scope = nullptr;	// scope hash list

	struct node_t
	{
		node_t* next;
		node_t* prev;
#ifdef _DEBUG
		node_t* _dbg_head;
#endif

		node_t()
		{
			reset();
		}

		void reset()
		{
			next = nullptr;
			prev = nullptr;
		}
	};
	node_t node_scope_sym_index;
};

template <typename K, typename V, uint32_t N, typename M>
using parallel_flat_hash_map_N = phmap::parallel_flat_hash_map<K, V, typename phmap::parallel_flat_hash_map<K, V>::hasher, typename phmap::parallel_flat_hash_map<K, V>::key_equal, typename phmap::parallel_flat_hash_map<K, V>::allocator_type, N, M>;
template <typename K, uint32_t N, typename M>
using parallel_flat_hash_set_N = phmap::parallel_flat_hash_set<K, typename phmap::parallel_flat_hash_set<K>::hasher, typename phmap::parallel_flat_hash_set<K>::key_equal, typename phmap::parallel_flat_hash_set<K>::allocator_type, N, M>;


class VAHashTable
{
  public:
	// consider having each Row have its own Pool, Reserve and lock (moved from the hashtable)
	// so that reading a row more likely benefits from cache line hits.
	// Would need to move all mem management and insert logic from table to row class.
	// Watch out for excessive memory use when each row has its own reserve.
	template<DefObj* const DefObj::*next>
	class _Row
	{
	  public:
		friend class VAHashTable;

		_Row() = default;

		_Row& operator=(nullptr_t)
		{
			mCount = 0;
			mHead = nullptr;
			return *this;
		}

		DefObj* Head() const
		{
			return mHead;
		}
		int Count() const
		{
			return mCount;
		}

	  private:
		// multiple reader (insert) threads possible; lock-free head insert
		void Insert(DefObj* obj)
		{
			for (;;)
			{
				const_cast<DefObj *&>(obj->*next) = mHead;
				if (InterlockedCompareExchangePointer((PVOID*)&mHead, obj, obj->*next) == obj->*next)
					break;
				// we were pre-empted, someone else inserted; retry
			}

			++mCount;
		}

		// Remove is protected by g_pDBLock::WriteLock
		bool Remove(DefObj* obj)
		{
			DefObj** pp = &mHead;
			DefObj* p = mHead;

			while (p)
			{
				if (p == obj)
				{
					// snip
					*pp = p->*next;
					--mCount;
					return true;
				}

				pp = &const_cast<DefObj*&>(p->*next);
				p = p->*next;
			}
			return false;
		}

		// Remove is protected by g_pDBLock::WriteLock
		void Remove(DefObj* obj, DefObj* prev)
		{
			_ASSERTE(obj);
			if (prev)
			{
				_ASSERTE(prev->*next == obj);
				const_cast<DefObj*&>(prev->*next) = obj->*next;
			}
			else
			{
				// if prev is NULL, then obj must be the row head
				_ASSERTE(mHead == obj);
				mHead = obj->*next;
			}

			--mCount;
		}

		_Row(const _Row& rhs) = delete;
		_Row& operator=(const _Row& rhs) = delete;

		std::atomic_int mCount = 0;
		DefObj* mHead = nullptr;
	};
	using Row = _Row<&DefObj::next>;
	using ScopeRow = _Row<&DefObj::next_scope>;

	VAHashTable(uint rows)
	    : mNumRows(rows), mNumItems(0), mTable(mNumRows), mScopeTable((rows >= mNumRows_scope_opt_cutoff) ? mNumScopeRows : 0)
	{
		if (HasScopeSymbolIndex())
			mScopeSymbolTable.reserve(rows * 10);
	}

	~VAHashTable()
	{
		DestroyTable();
	}

	Row* FindRow(uint hash) const
	{
		return &mTable[(hash & HASHCASEMASK) % mNumRows];
	}
	static constexpr uint GetScopeRowNum(uint scope_hash, uint sym_hash = 0)
	{
		// I don't know if HASHCASEMASK is needed for scopes, but it doesn't hurt
		uint row = (scope_hash & HASHCASEMASK) % mNumScopeRows_scope_hash;
		row *= mNumScopeRows_sym_hash;
		row += (sym_hash & HASHCASEMASK) % mNumScopeRows_sym_hash;
		return row;
	}
	uint GetScopeEndRowNum(uint scope_hash) const
	{
		return GetScopeRowNum(scope_hash) + mNumScopeRows_sym_hash;
	}
	ScopeRow* FindScopeRow(uint scope_hash, uint sym_hash) const
	{
		assert(HasScopeIndex());
		return &mScopeTable[GetScopeRowNum(scope_hash, sym_hash)];
	}
	DefObj* FindRowHead(uint hash) const
	{
		return mTable[(hash & HASHCASEMASK) % mNumRows].Head();
	}
	DefObj* FindScopeRowHead(uint scope_hash, uint sym_hash) const
	{
		return FindScopeRow(scope_hash, sym_hash)->Head();
	}
	static DefObj *ScopeSymbolNodeToDefObj(DefObj::node_t *n)
	{
		return n ? (DefObj*)((char*)n - offsetof(DefObj, node_scope_sym_index)) : nullptr;
	}
	DefObj* FindScopeSymbolHead(uint32_t scope_hash, uint32_t sym_hash) const
	{
		assert(HasScopeSymbolIndex());
		DefObj *ret = nullptr;
		mScopeSymbolTable.if_contains(make_scope_symbol_key(scope_hash, sym_hash), [&ret](va_table_t::value_type& v) {
			const auto& [key, value] = v;
			const auto& [list_head, cnt, _, signature] = *value;
			assert(signature == _dbg_signature);
			ret = ScopeSymbolNodeToDefObj(list_head.next);
		});
		return ret;
	}
	Row* GetRow(uint row) const
	{
		_ASSERTE(row < mNumRows);
		return &mTable[row];
	}
	ScopeRow* GetScopeRow(uint scope_row) const
	{
		assert(!mScopeTable.empty());
		_ASSERTE(scope_row < mNumScopeRows);
		return &mScopeTable[scope_row];
	}
	DefObj* GetRowHead(uint row) const
	{
		_ASSERTE(row < mNumRows);
		return mTable[row].Head();
	}
	DefObj* GetScopeRowHead(uint scope_row) const
	{
		assert(!mScopeTable.empty());
		_ASSERTE(scope_row < mNumScopeRows);
		return mScopeTable[scope_row].Head();
	}

	uint GetItemsInContainer() const
	{
		return mNumItems;
	}
	uint TableSize() const
	{
		return mNumRows;
	}
	uint ScopeTableSize() const
	{
		return (uint)mScopeTable.size();
	}
	bool HasScopeIndex() const
	{
		return !mScopeTable.empty();
	}
	bool HasScopeSymbolIndex() const
	{
#ifdef _WIN64
		return true;
#else
		return false;
#endif
	}

	// use only from a single thread!!
	void DoDetach(DefObj* d)
	{
		// we can't assert that the writing thread lock is this thread because
		// a concurrent walk of the table can have rows handled concurrently
		// on different threads -- so all we can do is assert that SOMEONE / anyone
		// has the write lock
		_ASSERTE(g_pDBLock->GetWritingThread() /* == ::GetCurrentThreadId()*/);
		Row* row = FindRow(d->SymHash());
		bool removed = row && row->Remove(d);

		bool scope_removed = true;
		if (HasScopeIndex())
		{
			ScopeRow* scope_row = FindScopeRow(d->ScopeHash(), d->SymHash());
			scope_removed = scope_row && scope_row->Remove(d);
			assert(removed == scope_removed);
		}
		if (HasScopeSymbolIndex())
			RemoveFromSymbolScopeTable(d);

		if(scope_removed && removed)
			ReleaseObj(d);
	}

	// The number of scopes is relatively small compared to the number of symbols; for some scopes, there is a good possibility a bucket would hold a large number of symbols.
	// So, for every scope_hash bucket index, we'll actually have '64' buckets that are additionally divided by a symbol hash to reduce a load.
#ifdef _WIN64
	static constexpr uint mNumScopeRows_scope_hash = 32768;
	static constexpr uint mNumScopeRows_sym_hash = 64;
#else
	static constexpr uint mNumScopeRows_scope_hash = 4096;
	static constexpr uint mNumScopeRows_sym_hash = 32;
#endif
	static constexpr uint mNumScopeRows = mNumScopeRows_scope_hash * mNumScopeRows_sym_hash;
	static constexpr uint mNumRows_scope_opt_cutoff = 200000;	// there are a lot of temporary hashmaps with 1000 buckets, we need scope index on two main ones with 200000 and 210000 buckets

	// collect scope-hash indices if used from multi-thread
	using locks_t = std::array<std::shared_mutex, mNumScopeRows>;
	using locks_ptr = std::unique_ptr<locks_t>;
	using do_detach_locks_t = std::tuple<std::once_flag, locks_ptr, std::atomic_uint64_t>;

// When symbols are being removed, they are removed in a parallel, each thread working on a separate row/bucket, so that was safe to do.
// However, when scope indexing is turned on, that cannot be done on scope buckets as they might be modified from different threads.
// To resolve that, I create a spinlock for every scope bucket, which is not a problem as:
//  - removal is quick, waiting thread shouldn't get recheduled
//  - srw locks don't allocate kernel resources; std::shared_mutex constructor just fills shared_mutex members with zeroes
	void DoDetachWithHint(Row* row, DefObj* d, DefObj* prev, do_detach_locks_t &do_detach_locks)
	{
		_ASSERTE(row);

		{
			std::unique_lock<std::shared_mutex> l;
			uint scope_row_num = 0;
			if (HasScopeIndex() || HasScopeSymbolIndex())
			{
				auto& [once_flag, locks, removed_items_count] = do_detach_locks;
				std::call_once(once_flag, [&] { locks = std::make_unique<locks_t>(); });

				scope_row_num = GetScopeRowNum(d->ScopeHash(), d->SymHash());
				++removed_items_count;
				l = std::unique_lock<std::shared_mutex>{locks->at(scope_row_num)};
			}

			if (HasScopeIndex())
			{
				ScopeRow* scope_row = GetScopeRow(scope_row_num);
				bool scope_removed = scope_row->Remove(d);
				assert(scope_removed);
			}
			if (HasScopeSymbolIndex())
				RemoveFromSymbolScopeTable(d, &l);

			row->Remove(d, prev);
		}
		d->node_scope_sym_index.reset();
		ReleaseObj(d);
	}
	static uint64_t FlushDoDetachLocks(do_detach_locks_t& do_detach_locks)
	{
		// no need to call explicitely if unneeded; destructor will free everything
		auto& [_, locks, removed_items_count] = do_detach_locks;
		uint64_t ret = removed_items_count;
		locks.reset();
		return ret;
	}

	void Flush()
	{
		// [case: 60806]
		Empty();
		get_ltalloc()->ltsqueeze(0);
	}

	void Empty()
	{
		if (!mNumItems)
			return;

		const uint numItems = mNumItems;
		int extCount = 0, privSysCount = 0, notDbBackedCount = 0, withDataStrs = 0;
		bool dumpAllItems = false;
		for (uint i = 0; i < mNumRows; i++)
		{
			DefObj* p = mTable[i].Head();
			if (!p)
				continue;

			mTable[i].mHead = NULL;
			if (g_loggingEnabled)
			{
				if (dumpAllItems)
				{
					for (DefObj* p1 = p; p1; p1 = p1->next)
					{
						p1->LoadStrs();
						vLog("hti: %s\t%s\n", p1->SymScope().c_str(), p1->Def().c_str());
					}
				}
				else if (200 < mTable[i].mCount)
				{
					vCatLog("Parser.VAHashTable", "  row(%d) items(%d)\n", i, mTable[i].mCount.load());
					if (10000 < mTable[i].mCount)
					{
						// log the first 20 items
						DefObj* p1 = p;
						for (int idx = 0; p1 && idx++ < 20; p1 = p1->next)
						{
							vCatLog("Parser.VAHashTable", "    item: %s\n", p1->SymScope().c_str());
						}
					}
				}

				for (DefObj* p1 = p; p1; p1 = p1->next)
				{
					if (p1->HasData())
						withDataStrs++;
					if (p1->IsDbExternalOther())
						extCount++;
					if (p1->IsDbSolutionPrivateSystem())
						privSysCount++;
					if (!p1->IsDbBackedByDataFile())
						notDbBackedCount++;
				}
			}

			do
			{
				DefObj* nxt = p->next;
				p->node_scope_sym_index.reset();
				ReleaseObj(p);
				p = nxt;
			} while (p);
		}
		_ASSERTE(0 == mNumItems);
		vCatLog("Parser.VAHashTable", "HashTbl::Empty  this(%p) rows(%d) items(%d) ext(%d) pv(%d) notBack(%d) ds(%d)\n", this, mNumRows,
		     numItems, extCount, privSysCount, notDbBackedCount, withDataStrs);

		std::fill(mScopeTable.begin(), mScopeTable.end(), nullptr);
		mScopeSymbolTable.clear();
	}

	DefObj* CreateEntry(DType&& dtype)
	{
		DefObj* reservedObj = (DefObj*)get_ltalloc()->ltmalloc(sizeof(DefObj));
		if (!reservedObj)
		{
			::Sleep(10);
			reservedObj = (DefObj*)get_ltalloc()->ltmalloc(sizeof(DefObj));
			if (!reservedObj)
			{
				VALOGERROR("ERROR: HashTbl::CreateEntry alloc failed\n");
				ASSERT_ONCE("HashTbl::CreateEntry alloc failed (ASSERT_ONCE)");
				return nullptr;
			}
		}

		DefObj* d = new (reservedObj) DefObj(std::move(dtype));
		if (d)
			Insert(d);
		return d;
	}

	DefObj* Find(uint symhash)
	{
		for (DefObj* obj = FindRowHead(symhash); NULL != obj; obj = obj->next)
		{
			if (obj->SymHash() == symhash)
				return obj;
		}
		return NULL;
	}

  private:
	VAHashTable();

	void DestroyTable()
	{
#ifndef VA_CPPUNIT
		Empty();
		mTable.clear();
		mScopeTable.clear();
		mScopeSymbolTable.clear();
		get_ltalloc()->ltsqueeze(0);
#endif
	}

	void Insert(DefObj* d)
	{
		_ASSERTE(g_pDBLock->GetWritingThread() == NULL);
		Row* row = FindRow(d->SymHash());
		if (row)
		{
			row->Insert(d);
			InterlockedIncrement((volatile LONG*)&mNumItems);
		}

		if (HasScopeIndex())
			FindScopeRow(d->ScopeHash(), d->SymHash())->Insert(d);

		if(HasScopeSymbolIndex())
		{
			va_table_t::key_type key = make_scope_symbol_key(d->ScopeHash(), d->SymHash());
			mScopeSymbolTable.lazy_emplace_l(
			    key,
			    [d](va_table_t::value_type& v) {
				    // list head already exists
				    auto& [_key, _value] = v;
				    auto& [list_head, cnt, key, signature] = *_value;
					auto &d_node = d->node_scope_sym_index;
				    assert(signature == _dbg_signature);

				    d_node.next = list_head.next;
					if(d_node.next)
						d_node.next->prev = &d_node;
				    d_node.prev = &list_head;
					list_head.next = &d_node;
#ifdef _DEBUG
					d_node._dbg_head = &list_head;
#endif
				    ++cnt;
					assert(key == _key);
			    },
			    [key, d](const va_table_t::constructor& ctor) {
				    // first element, create list head
				    auto head = std::make_unique<list_head_t>();
				    auto& [list_head, cnt, _key, signature] = *head;
				    auto& d_node = d->node_scope_sym_index;
				    list_head.next = &d_node;
				    d_node.prev = &list_head;
#ifdef _DEBUG
				    d_node._dbg_head = &list_head;
#endif
				    cnt = 1;
				    _key = key;
					signature = _dbg_signature;
				    ctor(key, std::move(head));
			    });
		}
	}

	void RemoveFromSymbolScopeTable(DefObj* d, std::unique_lock<std::shared_mutex> *l = nullptr)
	{
		auto &node = d->node_scope_sym_index;
		assert(node.prev); // there will always be a list head to point to

#ifdef _DEBUG
		{
			const auto offset_of_node = (uintptr_t)&std::get<DefObj::node_t>(*(list_head_t*)1) - 1;
			auto* head = (list_head_t*)((uintptr_t)node._dbg_head - offset_of_node);
			assert(std::get<3>(*head) == _dbg_signature);
			--std::get<1>(*head);
		}
#endif

		node.prev->next = node.next;
		if(node.next)
			node.next->prev = node.prev;
		else if(!node.prev->prev)
		{
			// node.prev points to the list head
			// offset_of_node is position of DefObj::node_t within list_head_t, so we can get list_head_t* from node_t*
			const auto offset_of_node = (uintptr_t)&std::get<DefObj::node_t>(*(list_head_t*)1) - 1;
			auto* head = (list_head_t*)((uintptr_t)node.prev - offset_of_node);
			auto key = std::get<2>(*head);
			assert(std::get<3>(*head) == _dbg_signature);

			if (l)
				l->unlock();
			mScopeSymbolTable.erase_if(key, [](va_table_t::value_type& v) {
				assert(std::get<1>(*v.second) == 0); // list needs to be empty here
				return true;
			});
		}
		node.reset();
	}


	void ReleaseObj(DefObj* p)
	{
		_ASSERTE(mNumItems);
		InterlockedDecrement((volatile LONG*)&mNumItems);
		p->~DefObj();
		const_cast<DefObj*&>(p->next) = nullptr;
		const_cast<DefObj*&>(p->next_scope) = nullptr;
		p->node_scope_sym_index.reset();
		get_ltalloc()->ltfree(p);
	}

	const uint mNumRows;
	volatile uint mNumItems;
	mutable std::vector<Row> mTable;				// indexed by symbol hash
	mutable std::vector<ScopeRow> mScopeTable;		// indexed by scope hash


	static constexpr uint64_t _dbg_signature = 0x1234567890abcdefull;
	using va_table_key_t = uint64_t;
	using list_head_t = std::tuple<DefObj::node_t, std::atomic_uint32_t, va_table_key_t, uint64_t>; // <head node, dbg element count, key, dbg signature>
	using list_head_ptr = std::unique_ptr<list_head_t>;
	using va_table_t = parallel_flat_hash_map_N<va_table_key_t, list_head_ptr, 8, std::shared_mutex/*phmap::srwlock*/>;
	static inline va_table_key_t make_scope_symbol_key(uint32_t scope_hash, uint32_t sym_hash)
	{
		return ((uint64_t)scope_hash << 32) | sym_hash;
	}
	mutable va_table_t mScopeSymbolTable; // indexed by symbol and scope
};

#endif // _VA_HASHTABLE_H_
