#ifndef fooh
#define fooh

#include "token.h"
#include "SymbolTypes.h"
#include <memory>
#include "Lock.h"
#include "remote_heap.h"
#include <assert.h>
#include <unordered_map>
#include <Win32Heap.h>
#include <atomic>

#ifdef _DEBUG
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(8) // the default
#endif

#pragma warning(disable : 4815)

class MultiParse;
extern WTString StrGetSymScope(LPCSTR symscope);
extern std::string_view StrGetSymScope_sv(LPCSTR symscope);
extern std::string_view StrGetSymScope_sv(std::string_view symscope);
extern LPCSTR StrGetSym(LPCSTR symscope);

enum SymbolVisibility
{
	vPublic = 1,
	vProtected,
	vInternal,
	vProtectedInternal,
	vPublished,
	vPrivate,
	vPrivateProtected
};

// strings are 3694 max, 33avg
struct SymDefStrStruct
{
  private:
	// [case: 70523] mSymScope and mDef are const because they are unprotected "globals"
	// mSymScope string follows this struct in memory
	// mDef string follows mSymScope string if it's not empty, which is determined with highest mSymOffsetAndRefCount
	// DefEmpty bit (in 99% cases it's empty)

	uint16_t scope_len = 0; // includes null-terminator
	uint16_t def_len = 0;   // includes null-terminator
	uint16_t sym_offset = 0;

  public:
	[[nodiscard]] uint SymOffset() const
	{
		return sym_offset;
	}
	[[nodiscard]] const char* SymPtr() const
	{
		return &PrivateGetSymScopeStr()[SymOffset()];
	}
	[[nodiscard]] bool IsDefEmpty() const
	{
		return def_len <= 1;
	}
	void InitSym()
	{
		if (!sym_offset && HasScope())
		{
			uint offset = CalcSymOffset();
			sym_offset = (uint16_t)offset;
			assert(offset == sym_offset); // range check
		}
	}
	[[nodiscard]] uint CalcSymOffset() const
	{
		// [case: 74097]
		const char* tmp = ::StrGetSym(PrivateGetSymScopeStr());
		return ptr_sub__uint(tmp, PrivateGetSymScopeStr());
	}
	[[nodiscard]] size_t GetSize() const
	{
		return sizeof(*this) + scope_len + def_len;
	}
	[[nodiscard]] size_t GetScopeStrLen() const
	{
		return (scope_len > 0) ? (scope_len - 1) : 0u;
	} // not including null-terminator
	[[nodiscard]] size_t GetDefStrLen() const
	{
		return (def_len > 0) ? (def_len - 1) : 0u;
	} // not including null-terminator

  public:
	SymDefStrStruct() = default;
	~SymDefStrStruct() = default;

	static size_t Emplace(void* buffer, const WTString& s, const WTString& d, size_t buffer_size)
	{
		return Emplace(buffer, s.c_str(), (size_t)s.GetLength(), d.c_str(), (size_t)d.GetLength(), buffer_size);
	}

	// unused; fixed, but commented out as it's untested
	[[nodiscard]] bool operator==(const SymDefStrStruct& s1) const = delete;
	//	{
	//		if (!HasScope() || !s1.HasScope())
	//		{
	////			_ASSERTE(!"don't compare SymDefStrStruct unless string loaded");
	//			return HasScope() == s1.HasScope();
	//		}
	//
	//		if (!strcmp(PrivateGetSymScopeStr(), s1.PrivateGetSymScopeStr()))
	//		{
	//			if(!HasDef() || !s1.HasDef())
	//				return HasDef() == s1.HasDef();
	//			if (!strcmp(PrivateGetDefStr(), s1.PrivateGetDefStr()))
	//				return true;
	//		}
	//
	//		return false;
	//	}

	[[nodiscard]] WTString GetDef() const
	{
		return WTString(PrivateGetDefStr());
	}
	[[nodiscard]] static bool IsEmpty(SymDefStrStruct* sdss)
	{
		return !HasData(sdss);
	}
	[[nodiscard]] static bool HasData(SymDefStrStruct* sdss)
	{
		return sdss && sdss->HasData();
	}
	[[nodiscard]] bool HasData() const
	{
		return HasScope() || HasDef();
	}
	[[nodiscard]] bool HasDef() const
	{
		return def_len > 0;
	}
	[[nodiscard]] bool HasScope() const
	{
		return scope_len > 0;
	}
	[[nodiscard]] WTString GetSymScope() const
	{
		return WTString(PrivateGetSymScopeStr());
	}
	[[nodiscard]] WTString GetScope() const
	{
		return ::StrGetSymScope(PrivateGetSymScopeStr());
	}
	[[nodiscard]] WTString GetSym()
	{
		return WTString(SymPtr());
	}

  private:
	static size_t Emplace(void* buffer, const char* s_str, size_t s_len, const char* d_str, size_t d_len, size_t buffer_size);

  public:
	[[nodiscard]] const char* PrivateGetSymScopeStr() const
	{
		//		assert(StringsHasBeenRead());
		assert(HasScope());
		return HasScope() ? GetTrailingBytes() : nullptr;
	}
	[[nodiscard]] const char* PrivateGetDefStr() const
	{
		//		assert(StringsHasBeenRead());
		assert(HasDef());
		return HasDef() ? &GetTrailingBytes()[scope_len] : nullptr;
	}

	[[nodiscard]] char* GetTrailingBytes()
	{
		return (char*)(this + 1);
	}
	[[nodiscard]] const char* GetTrailingBytes() const
	{
		return (const char*)(this + 1);
	}
};

#pragma pack(push, 1)
// will be allocated in the dummy process
struct sdata
{
	// struct extended to hold strings info
	uint16_t total_strings_size = 0;
	// add more members here
	SymDefStrStruct mSymData;

	[[nodiscard]] bool contains_strings() const
	{
		return total_strings_size > 0;
	}
	[[nodiscard]] size_t get_size() const
	{
		return sizeof(*this) + total_strings_size;
	}
	[[nodiscard]] static size_t get_size(const WTString* symscope, const WTString* def)
	{
		return sizeof(sdata) + (symscope ? symscope->GetLength() : 0) + 1 +
		       ((def && def->GetLength()) ? (def->GetLength() + 1) : 0);
	}
};
static const inline size_t max_strings_size = remote_heap::max_block_size - sizeof(sdata);
#pragma pack(pop)

// [case:142114]
// DType is written to/read from VADbFiles.
#pragma pack(push, 4)
class DType
{
	friend class DBFilePair;

  protected:
	// do not change order -- datafiles are read directly into this
	// (see MultiParse::ReadIdxData and DBFilePair::DBOut).
	uint mTypeAndDbFlags = 0; // TYPEMASK | VA_DB_FLAGS
	uint mAttributes = 0;
	uint mScopeHash = 0; // hash of scope
	uint mSymHash = 0;   // hash of symbol (primary sym hash)
	uint mSymHash2 = 0;  // secondary/alternative sym hash, used to prevent load of strings during SymMatch (avoids
	                     // collision of primary hash)

	static const int mLine_bits = 22;
	static const int mSplitIdx_bits = 10;
	static_assert((mLine_bits + mSplitIdx_bits) == 32);

	uint mFileID = 0;               // fileId (per gFileIdManager) of file where symbol was located; (24 bits minimum)
	uint mIdxOffset = 0;            // dbfile: offset into the idx file
	int mLine : mLine_bits;         // measured range 0-340264, 19 bits
	int mSplitIdx : mSplitIdx_bits; // dbfile: idx file split number

	mutable remote_ptr sdata_rptr;
	mutable std::atomic_int mReadWriteCount = 0; // > 0 active readers, < 0 active writer

	static void CheckLineRange(int l)
	{
		assert(((l >> (mLine_bits - 1)) == 0) || ((l >> (mLine_bits - 1)) == -1));
	}
	static void CheckSplitIdxRange(uint si)
	{
		assert((si >> mSplitIdx_bits) == 0);
	}

	class sdata_map
	{
	  public:
		sdata_map(const DType* dtype) : dtype(dtype)
		{
			assert(dtype);
		}
		sdata_map(sdata_map&& other) : sdata_map(other.dtype)
		{
#ifndef NO_REMOTE_HEAP
			if (other.mapped)
				mapped.emplace(std::move(*other.mapped));
#endif
		}

		const sdata& get() const
		{
			const sdata* ret = get_ptr_const();
			assert(ret);
			return *ret;
		}
		const sdata* operator->() const
		{
			// will return empty sdata on error
			return get_ptr_const();
		}
		const sdata* get_ptr_const() const
		{
			// will return empty sdata on error
			assert(dtype);
			if (!dtype)
				return nullptr;
			if (!dtype->sdata_rptr)
				return &empty_sdata;

#ifndef NO_REMOTE_HEAP
			if (!mapped)
				mapped.emplace(dtype->sdata_rptr);
			auto ret = (sdata*)mapped->get_block_ptr();
			return ret ? ret : &empty_sdata;
#else
			return dtype->sdata_rptr;
#endif
		}
		sdata* get_ptr()
		{
			// will return nullptr on error
			assert(dtype);
			assert(dtype->sdata_rptr);
			if (!dtype || !dtype->sdata_rptr)
				return nullptr;

#ifndef NO_REMOTE_HEAP
			if (!mapped)
				mapped.emplace(dtype->sdata_rptr);
			return (sdata*)mapped->get_block_ptr();
#else
			return dtype->sdata_rptr;
#endif
		}

		void copy_from(const DType* from);
		// returns true if block pointer changed
		bool add_strings(const WTString& symscope, const WTString& def)
		{
			bool ret = false;
			assert(dtype);
			if (!dtype)
				return ret;

			remote_heap::stats::timing t(remote_heap::stats::stat::timing_sdatamap_addstrings_us,
			                             remote_heap::stats::stat::timing_sdatamap_addstrings_cnt);

			size_t size = sdata::get_size(&symscope, &def);
			if (size > max_strings_size)
			{
				// This is just to be complete. VA Parser seems to give up at 750 characters for name and about 4k for
				// def.
				#ifdef _DEBUG
				static bool once = true;
				if(once)
				{
					assert(!"strings too large");
					once = false;
				}
				#endif
				return add_strings(":___string_too_large", "");
			}
			remote_ptr to_delete;
			if (dtype->sdata_rptr)
			{
				// try to reuse existing block
#ifndef NO_REMOTE_HEAP
				size_t block_size =
				    std::get<remote_heap::remote_heap_impl::area*>(dtype->sdata_rptr.deconstruct())->block_size;
#else
				size_t block_size = remote_heap::remote_heap_impl::get_block_size(dtype->sdata_rptr);
#endif
				if (block_size < size)
				{
					to_delete = delete_rptr_no_dealloc(); // just to be sure if new strings contain parts of the old string
					remote_heap::stats::increase_counter(remote_heap::stats::stat::sdatamap_block_not_reused);
				}
				else
					remote_heap::stats::increase_counter(remote_heap::stats::stat::sdatamap_block_reused);
			}
			if (!dtype->sdata_rptr)
			{
				dtype->sdata_rptr = rheap->alloc(size);
				ret = true;
			}
			assert(dtype->sdata_rptr);
			if (!dtype->sdata_rptr)
				return false; // unlikely to fail
			if (!get_ptr())
				return false; // mapping failed

			new (get_ptr()) sdata;
			SymDefStrStruct::Emplace(&get_ptr()->mSymData, symscope, def, size - (sizeof(sdata) - sizeof(sdata::mSymData)));
			assert((size - sizeof(sdata)) <= std::numeric_limits<uint16_t>::max());
			get_ptr()->total_strings_size = uint16_t(size - sizeof(sdata));
			if(to_delete)
				rheap->dealloc(to_delete);
			return ret;
		}
		void delete_rptr()
		{
			if (!dtype || !dtype->sdata_rptr)
				return;
#ifndef NO_REMOTE_HEAP
			mapped = std::nullopt;
#endif
			rheap->dealloc(dtype->sdata_rptr);
			dtype->sdata_rptr = nullptr;
		}
		remote_ptr delete_rptr_no_dealloc()
		{
			if (!dtype || !dtype->sdata_rptr)
				return {};
#ifndef NO_REMOTE_HEAP
			mapped = std::nullopt;
#endif
			return std::exchange(dtype->sdata_rptr, nullptr);
		}

	  protected:
		const DType* const dtype;
#ifndef NO_REMOTE_HEAP
		mutable std::optional<mapped_block> mapped;
#endif

		static sdata empty_sdata;
	};

  public:
#ifdef _DEBUG
	static thread_local std::unordered_set<std::atomic_int *> deadlock_check;
#endif
	class LockForDtypeStringWrite
	{
		std::atomic_int& mReadWriteLock;

	  public:
		static constexpr int kWriterLockValue = 65536;

		LockForDtypeStringWrite() = delete;
		LockForDtypeStringWrite(const DType* dtype) : LockForDtypeStringWrite((assert(dtype), dtype->mReadWriteCount))
		{
		}

	  private:
		LockForDtypeStringWrite(std::atomic_int& readWriteCnt) : mReadWriteLock(readWriteCnt)
		{
			for (;;)
			{
				int wanted = 0;
				if (mReadWriteLock.compare_exchange_weak(wanted, -kWriterLockValue))
				{
#ifdef _DEBUG
					deadlock_check.insert(&mReadWriteLock);
#endif
					return;
				}

				remote_heap::stats::increase_counter(remote_heap::stats::stat::write_wait);
				::SwitchToThread(); // seems to be preferred than Sleep(0);. Could be optimized using WaitOnAddress, but
				                    // this is very rarely executed anyway

				assert(!deadlock_check.contains(&mReadWriteLock));
			}
		}

	  public:
		~LockForDtypeStringWrite()
		{
			mReadWriteLock += kWriterLockValue;
#ifdef _DEBUG
			deadlock_check.erase(&mReadWriteLock);
#endif
		}
	};

	class CheckoutForDtypeStringRead
	{
		std::atomic_int* readWriteCnt = nullptr;

	  public:
		CheckoutForDtypeStringRead() = default;
		CheckoutForDtypeStringRead(CheckoutForDtypeStringRead&& other) : readWriteCnt(other.readWriteCnt)
		{
			other.readWriteCnt = nullptr;
		}
		CheckoutForDtypeStringRead(const CheckoutForDtypeStringRead& other)
		    : CheckoutForDtypeStringRead(other.readWriteCnt)
		{
		}
		CheckoutForDtypeStringRead(const DType* dtype)
		    : CheckoutForDtypeStringRead(dtype ? &dtype->mReadWriteCount : nullptr)
		{
		}

	  private:
		CheckoutForDtypeStringRead(std::atomic_int* readWriteCnt) : readWriteCnt(readWriteCnt)
		{
			if (!readWriteCnt)
				return;

			for (;;)
			{
				if (++*readWriteCnt > 0)
				{
#ifdef _DEBUG
					deadlock_check.insert(readWriteCnt);
#endif
					return;
				}

				int newVal = --*readWriteCnt;
				_ASSERTE(newVal < (-LockForDtypeStringWrite::kWriterLockValue + 1000) ||
				         newVal >= 0); // before locking was correct, I would sometimes see a value of -1
				std::ignore = newVal;

				remote_heap::stats::increase_counter(remote_heap::stats::stat::read_wait);
				::SwitchToThread();

				assert(!deadlock_check.contains(readWriteCnt));
			}
		}

	  public:
		~CheckoutForDtypeStringRead()
		{
			release();
		}

		void release()
		{
			if (readWriteCnt)
			{
				--*readWriteCnt;
#ifdef _DEBUG
				deadlock_check.erase(readWriteCnt);
#endif
				readWriteCnt = nullptr;
			}
		}
	};

	struct symstrs_t
	{
		// WARNING!! 'rslock' has to appear BEFORE 'map' member!!
		CheckoutForDtypeStringRead rslock; // ensures that the mapping is not deleted while we are alive (typically as a temporary)
		std::optional<sdata_map> map; // while mapped, scope and def pointers are good
		const char* scope = nullptr;
		const char* def = nullptr;
		uint32_t sym_offset = 0;

		using locked_string_view = std::pair<std::string_view, CheckoutForDtypeStringRead>;

	private:
		[[nodiscard]] std::string_view GetSymScope_sv_nolock() const
		{
			if (scope)
			{
				size_t len = map->get().mSymData.GetScopeStrLen();
				assert(!scope[len]);
				return {scope, len};
			}
			else
				return {};
		}
		[[nodiscard]] std::string_view GetDef_sv_nolock() const
		{
			if (def)
			{
				size_t len = map->get().mSymData.GetDefStrLen();
				assert(!def[len]);
				return {def, len};
			}
			else
				return {};
		}
		[[nodiscard]] const char* SymPtr() const
		{
			if (!map || !scope)
				return nullptr;
			assert(scope || !sym_offset);
			return &scope[sym_offset];
		}
		[[nodiscard]] std::string_view GetSym_sv_nolock() const
		{
			const char* sym = SymPtr();
			if (sym)
				return GetSymScope_sv_nolock().substr(sym_offset);
			else
				return {};
		}

	  public:
	    [[nodiscard]] WTString GetSymScope() const
		{
			return GetSymScope_sv_nolock();
		}
		[[nodiscard]] locked_string_view GetSymScope_sv() const
		{
			if(scope)
				return {GetSymScope_sv_nolock(), rslock};
			else
				return {"", {}};
		}
		[[nodiscard]] WTString GetDef() const
		{
			return GetDef_sv_nolock();
		}
		[[nodiscard]] locked_string_view GetDef_sv() const
		{
			if (def)
				return {GetDef_sv_nolock(), rslock};
			else
				return {"", {}};
		}
		[[nodiscard]] uint SymOffset() const
		{
			return sym_offset;
		}

		[[nodiscard]] bool HasData() const
		{
			return HasScope() || HasDef();
		}
		[[nodiscard]] bool HasDef() const
		{
			return def && def[0];
		}
		[[nodiscard]] bool HasScope() const
		{
			return scope && scope[0];
		}

		[[nodiscard]] WTString GetSym() const
		{
			return GetSym_sv_nolock();
		}
		[[nodiscard]] locked_string_view GetSym_sv() const
		{
			std::string_view sv = GetSym_sv_nolock();
			if (!sv.empty())
				return {sv, rslock};
			else
				return {"", {}};
		}
		[[nodiscard]] WTString GetScope() const
		{
			return ::StrGetSymScope_sv(GetSymScope_sv_nolock());
		}
		[[nodiscard]] locked_string_view GetScope_sv() const
		{
			auto sv = ::StrGetSymScope_sv(GetSymScope_sv_nolock());
			if(!sv.empty())
				return {sv, rslock};
			else
				return {};
		}
		[[nodiscard]] bool SymScopeStartsWith(LPCSTR str) const
		{ /*assert(scope);*/
			return ::StartsWith(scope, str, true);
		}
		[[nodiscard]] bool DefStartsWith(LPCSTR str) const
		{ /*assert(scope);*/
			return ::StartsWith(def, str, true);
		}

		[[nodiscard]] const symstrs_t* operator->() const
		{
			return this;
		}
		[[nodiscard]] bool operator==(const symstrs_t& right) const
		{
			bool ret = (!!scope == !!right->scope) && (!scope || !strcmp(scope, right->scope)) &&
			           (!!def == !!right->def) && (!def || !strcmp(def, right->def));
			assert(!ret || (sym_offset == right->sym_offset));
			return ret;
		}
	};

	using reason = remote_heap::stats::reason;

	[[nodiscard]] bool operator==(const DType& rhs) const
	{
		if (mTypeAndDbFlags != rhs.mTypeAndDbFlags)
			return false;
		if (Attributes() != rhs.Attributes())
			return false;

		if (ScopeHash() != rhs.ScopeHash())
			return false;
		if (SymHash() != rhs.SymHash())
			return false;
		if (SymHash2() != rhs.SymHash2())
			return false;
		if (FileId() != rhs.FileId())
			return false;
		if (GetDbSplit() != rhs.GetDbSplit())
			return false;
		if (GetDbOffset() != rhs.GetDbOffset())
			return false;
		if (Line() != rhs.Line())
			return false;
		// ignore mSymData
		return true;
	}

	[[nodiscard]] uint ScopeHash() const
	{
		return mScopeHash;
	}
	[[nodiscard]] uint SymHash() const
	{
		return mSymHash;
	}
	[[nodiscard]] uint SymHash2() const
	{
		return mSymHash2;
	}
	[[nodiscard]] uint FileId() const
	{
		return mFileID;
	}
	void SetFileId(uint fid)
	{
		mFileID = fid;
	}
	[[nodiscard]] int Line() const
	{
		return mLine;
	}
	[[nodiscard]] uint Attributes() const
	{
		return mAttributes;
	}
	[[nodiscard]] uint DbFlags() const
	{
		return mTypeAndDbFlags & VA_DB_FLAGS;
	}
	[[nodiscard]] uint MaskedType() const
	{
		return mTypeAndDbFlags & TYPEMASK;
	}
	[[nodiscard]] uint type() const
	{
		return MaskedType();
	}
	void setType(uint type, uint attrs, uint dbFlags)
	{
		_ASSERTE(type != -1 && mAttributes != -1);
		_ASSERTE((!IsDbNet() && !IsDbCpp()) || IsSysLib());
		mTypeAndDbFlags = type | dbFlags;
		mAttributes = (mAttributes & V_DETACH) ? (V_DETACH | attrs) : attrs;
	}
	void copyType(const DType* dtype)
	{
		if (dtype)
		{
			mTypeAndDbFlags = dtype->mTypeAndDbFlags;
			mAttributes = dtype->mAttributes;
		}
		else
			mAttributes = mTypeAndDbFlags = 0;

		_ASSERTE(mTypeAndDbFlags != -1);
		_ASSERTE((!IsDbNet() && !IsDbCpp()) || IsSysLib());
	}

	[[nodiscard]] bool IsType() const
	{
		return IS_OBJECT_TYPE(MaskedType());
	}
	[[nodiscard]] bool IsMethod() const
	{
		return MaskedType() == FUNC;
	}
	[[nodiscard]] bool IsImpl() const
	{
		return !!(mAttributes & V_IMPLEMENTATION);
	}
	[[nodiscard]] bool IsDecl() const
	{
		return IsMethod() && !IsImpl();
	}
	[[nodiscard]] bool IsEquivalentType(uint otherType) const
	{
		if (otherType == type())
			return true;
		if (GOTODEF == otherType || FUNC == otherType)
			return FUNC == type() || GOTODEF == type();
		return false;
	}

	[[nodiscard]] bool infile() const
	{
		return !!(mAttributes & V_INFILE);
	}
	[[nodiscard]] bool inproject() const
	{
		return !!(mAttributes & V_INPROJECT);
	}
	// HasLocalFlag() gives different results than HasLocalScope()
	[[nodiscard]] bool HasLocalFlag() const
	{
		return !!(mAttributes & V_LOCAL);
	}
	[[nodiscard]] bool IsPointer() const
	{
		return !!(mAttributes & V_POINTER);
	}
	void SetPointer()
	{
		mAttributes |= V_POINTER;
	}
	[[nodiscard]] bool IsPrivate() const
	{
		return !!(mAttributes & V_PRIVATE);
	}
	[[nodiscard]] bool IsProtected() const
	{
		return !!(mAttributes & V_PROTECTED);
	}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	// [case: 135860]
	[[nodiscard]] bool IsInternal() const
	{
		return false;
	}
	[[nodiscard]] bool IsPublished() const
	{
		return !!(mAttributes & V_PUBLISHED);
	}
#else
	// [case: 135860]
	[[nodiscard]] bool IsInternal() const
	{
		return !!(mAttributes & V_INTERNAL);
	}
	[[nodiscard]] bool IsPublished() const
	{
		return false;
	}
#endif
	[[nodiscard]] bool IsPublic() const
	{
		return !IsPrivate() && !IsProtected() && !IsInternal() && !IsPublished();
	}
	[[nodiscard]] SymbolVisibility GetVisibility() const;
	[[nodiscard]] bool HasFileFlag() const
	{
		return !!(mAttributes & V_FILENAME);
	}
	[[nodiscard]] bool IsConstructor() const
	{
		return !!(mAttributes & V_CONSTRUCTOR);
	}
	[[nodiscard]] bool IsReservedType() const
	{
		return !!(mAttributes & V_RESERVEDTYPE) && (type() == RESWORD);
	}
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	[[nodiscard]] bool IsCppbClassMethod() const
	{
		return !!(mAttributes & V_CPPB_CLASSMETHOD) && (type() == FUNC || type() == GOTODEF);
	}
#endif
	[[nodiscard]] bool IsPreferredDef() const
	{
		return !!(mAttributes & V_PREFERREDDEFINITION);
	}
	[[nodiscard]] bool IsVaStdAfx() const
	{
		return !!(mAttributes & V_VA_STDAFX);
	}
	[[nodiscard]] bool IsTemplateItem() const
	{
		return !!(mAttributes & V_TEMPLATE_ITEM);
	}
	[[nodiscard]] bool IsDontExpand() const
	{
		return !!(mAttributes & V_DONT_EXPAND);
	}
	[[nodiscard]] bool IsIncludedMember() const
	{
		return !!(mAttributes & V_INCLUDED_MEMBER);
	}
	[[nodiscard]] bool IsHideFromUser() const
	{
		return !!(mAttributes & V_HIDEFROMUSER);
	}
	[[nodiscard]] bool IsTemplate() const
	{
		return !!(mAttributes & V_TEMPLATE);
	}
	[[nodiscard]] bool IsManaged() const
	{
		return !!(mAttributes & V_MANAGED);
	}
	void MarkForDetach()
	{
		mAttributes |= V_DETACH;
	}
	[[nodiscard]] bool IsMarkedForDetach() const
	{
		return !!(mAttributes & V_DETACH);
	}
	void FlipExpand()
	{
		mAttributes ^= V_DONT_EXPAND;
	}
	void DontExpand()
	{
		mAttributes |= V_DONT_EXPAND;
	}
	[[nodiscard]] bool IsSealed() const
	{
		return !!(mAttributes & V_SEALED);
	}
	[[nodiscard]] bool IsStatic() const
	{
		return !!(mAttributes & V_STATIC);
	}
	[[nodiscard]] bool IsConst() const
	{
		return !!(mAttributes & V_CONST);
	}
	[[nodiscard]] bool IsAbstract() const
	{
		return !!(mAttributes & V_ABSTRACT_CLASS) && IS_OBJECT_TYPE(type());
	}
	[[nodiscard]] bool IsPureMethod() const
	{
		return !!(mAttributes & V_PURE_METHOD) && (type() == FUNC);
	}
	[[nodiscard]] bool IsPartial() const
	{
		return !!(mAttributes & V_PARTIAL_CLASS) && IS_OBJECT_TYPE(type());
	}
	[[nodiscard]] bool IsNewMethod() const
	{
		return !!(mAttributes & V_NEW_METHOD) && (type() == FUNC || type() == GOTODEF);
	}
	[[nodiscard]] bool IsVirtualOrExtern() const
	{
		return !!(
		    mAttributes &
		    (V_EXTERN | V_VIRTUAL)); /* OR'd just to make it explicitly match the name - yes they have the same value */
	}
	[[nodiscard]] bool IsRef() const
	{
		return !!(mAttributes & V_REF) && (IS_OBJECT_TYPE(type()) || VAR == type());
	}
	[[nodiscard]] bool IsHashtagRef() const
	{
		return !!(mAttributes & V_REF) && (type() == vaHashtag);
	}
	[[nodiscard]] bool IsInline() const
	{
		return !!(mAttributes & V_INLINE) && (type() == FUNC || type() == GOTODEF);
	}
	[[nodiscard]] bool IsOverride() const
	{
		return !!(mAttributes & V_OVERRIDE);
	}

	[[nodiscard]] CStringW FilePath() const;
	[[nodiscard]] CStringW GetSymbolLocation(int& lineOut) const;

	[[nodiscard]] bool IsDbCpp() const
	{
		return !!(mTypeAndDbFlags & VA_DB_Cpp);
	}
	[[nodiscard]] bool IsDbNet() const
	{
		return !!(mTypeAndDbFlags & VA_DB_Net);
	}
	[[nodiscard]] bool IsDbSolution() const
	{
		return !!(mTypeAndDbFlags & VA_DB_Solution);
	}
	[[nodiscard]] bool IsDbBackedByDataFile() const
	{
		return !!(mTypeAndDbFlags & VA_DB_BackedByDatafile);
	}
	[[nodiscard]] bool IsDbExternalOther() const
	{
		return !!(mTypeAndDbFlags & VA_DB_ExternalOther);
	}
	[[nodiscard]] bool IsDbLocal() const
	{
		return !!(mTypeAndDbFlags & VA_DB_LocalsParseAll);
	}
	[[nodiscard]] bool IsDbSolutionPrivateSystem() const
	{
		return !!(mTypeAndDbFlags & VA_DB_SolutionPrivateSystem);
	}
	[[nodiscard]] DWORD GetDbSplit() const
	{
		return (DWORD)mSplitIdx;
	}
	[[nodiscard]] DWORD GetDbOffset() const
	{
		return mIdxOffset;
	}

	// These are not equivalent.  There is huge overlap, but some syms are
	// only V_SYSLIB (without DbNet or DbCpp), but sys locals are DbNet or
	// DbCpp (without V_SYSLIB).  This note is about actual usage, not
	// intended usage.
	[[nodiscard]] bool IsSysLib() const
	{
		return !!(mAttributes & V_SYSLIB);
	}
	[[nodiscard]] bool IsSystemSymbol() const
	{
		return IsSysLib() || IsDbNet() || IsDbCpp();
	}

	[[nodiscard]] bool IsEmpty() const
	{
		return (0 == mTypeAndDbFlags) && (0 == mAttributes);
	}

  public:
	~DType();

	DType() : mLine(0), mSplitIdx(0)
	{
		increase_counter(remote_heap::stats::stat::dtypes);
		increase_counter(remote_heap::stats::stat::dtypes_bytes, sizeof(*this));
	}

	DType(DType&& s) : mLine(0), mSplitIdx(0)
	{
		*this = std::move(s);
		assert(!s.mReadWriteCount);
		mReadWriteCount = (int)s.mReadWriteCount;
		s.mReadWriteCount = 0;
		increase_counter(remote_heap::stats::stat::dtypes);
		increase_counter(remote_heap::stats::stat::dtypes_bytes, sizeof(*this));
		//		increase_counter(remote_heap::stats::stat::dtypes_move);  counted in operator =
	}

	DType(const DType& s) : DType(&s)
	{
	}
	DType(const DType* s) : mLine(0), mSplitIdx(0)
	{
		remote_heap::stats::timing t(remote_heap::stats::stat::timing_DType_copy_us,
		                             remote_heap::stats::stat::timing_DType_copy_cnt);

		if (s)
		{
			mFileID = s->mFileID;
			mSplitIdx = s->mSplitIdx;
			mIdxOffset = s->mIdxOffset;
			mScopeHash = s->mScopeHash;
			mSymHash = s->mSymHash;
			mSymHash2 = s->mSymHash2;
			mLine = s->mLine;
			mTypeAndDbFlags = s->mTypeAndDbFlags;
			mAttributes = s->mAttributes;
			sdata_map(this).copy_from(s);
		}
// 		_ASSERTE(mIdxOffset != NPOS || !IsDbBackedByDataFile());
// 		_ASSERTE(mSplitIdx != /*SplitIdx_*/ NPOS || !IsDbBackedByDataFile());
		_ASSERTE(mTypeAndDbFlags != -1);
		_ASSERTE(mAttributes != -1);

		increase_counter(remote_heap::stats::stat::dtypes);
		increase_counter(remote_heap::stats::stat::dtypes_copy);
		increase_counter(remote_heap::stats::stat::dtypes_bytes, sizeof(*this));
	}

	//	DType(WTString symscope)
	//	{
	//		mSymData = SymDefStrStruct::Create(symscope);
	//		UpdateHashes(symscope);
	//	}

	DType(WTString symscope, WTString def, uint type, uint attrs, uint dbFlags, int ln = 0, UINT fileId = 0)
	    : DType(0, 0, 0, symscope, def, type, attrs, dbFlags, ln, fileId)
	{
	}

	DType(uint scopeHash, uint symHash, uint symHash2, WTString symscope, WTString def, uint type, uint attrs,
	      uint dbFlags, int ln, UINT fileId, uint idxoffset = (uint)NPOS, int splitidx = (int)NPOS)
	{
		mScopeHash = scopeHash;
		mSymHash = symHash;
		mSymHash2 = symHash2;
		mTypeAndDbFlags = type | dbFlags, mAttributes = attrs;
		mLine = ln;
		mFileID = fileId;
		mIdxOffset = idxoffset;
		mSplitIdx = splitidx;

		DType::CheckLineRange(ln);
		if (!scopeHash && !symHash)
		{
			mSymHash = GetSymHash(symscope);
			if (type == CachedBaseclassList)
				mSymHash2 = GetSymHash2(symscope.c_str());
			else
				mSymHash2 = GetSymHash2(StrGetSym(symscope.c_str()));
			mScopeHash = GetScopeHash(symscope);
		}
		_ASSERTE(mIdxOffset != NPOS || !IsDbBackedByDataFile());
		_ASSERTE(mSplitIdx != NPOS || !IsDbBackedByDataFile());
		_ASSERTE(IsDbBackedByDataFile() || (mSplitIdx == NPOS && mIdxOffset == NPOS));
		_ASSERTE(mTypeAndDbFlags != -1);
		_ASSERTE(mAttributes != -1);

		if (!symscope.IsEmpty() || !def.IsEmpty())
		{
			increase_counter(remote_heap::stats::stat::dtypes_had_no_strings);

			sdata_map(this).add_strings(symscope, def);
		}

		increase_counter(remote_heap::stats::stat::dtypes);
		increase_counter(remote_heap::stats::stat::dtypes_bytes, sizeof(*this));
	}

	DType& operator=(DType&& rhs)
	{
		increase_counter(remote_heap::stats::stat::dtypes_move);

		*this = nullptr;
		memcpy(this, &rhs, sizeof(rhs));
		memset(&rhs, 0, sizeof(rhs));
		_ASSERTE(0 == mReadWriteCount);
		return *this;
	}
	DType& operator=(const DType& rhs)
	{
		return operator=(std::move(DType(rhs)));
	}
	DType& operator=(nullptr_t); // main cleanup routine

	// LoadStrs loads both SymScope and Def.   Otherwise, load of SymScope does not load Def.
	void LoadStrs(bool force = false) const;
	void ReloadStrs();
	[[nodiscard]] bool HasData() const
	{
		// warning: be sure either GetStrs() or LoadStrs(true) is called before!
		return !!sdata_rptr;
	}
	[[nodiscard]] bool HasEquivalentSymData(const DType& rhs) const
	{
		symstrs_t s1 = GetStrs(true);
		symstrs_t s2 = rhs.GetStrs(true);
		increase_counter((remote_heap::stats::stat)(size_t)remote_heap::stats::reason::HasSameSymData);
		return s1 == s2;
	}
	[[nodiscard]] bool IsEquivalentIgnoringDbBacking(const DType& rhs) const;

	[[nodiscard]] WTString Def() const
	{
		return GetStrs(true)->GetDef();
	}
	[[nodiscard]] symstrs_t::locked_string_view Def_sv() const
	{
		return GetStrs(true)->GetDef_sv();
	}
	// returns scope + sym
	[[nodiscard]] WTString SymScope() const
	{
		return GetStrs()->GetSymScope();
	}
	[[nodiscard]] symstrs_t::locked_string_view SymScope_sv() const
	{
		return GetStrs()->GetSymScope_sv();
	}
	// returns scope without sym
	[[nodiscard]] WTString Scope() const
	{
		return GetStrs()->GetScope();
	}
	[[nodiscard]] symstrs_t::locked_string_view Scope_sv() const
	{
		return GetStrs()->GetScope_sv();
	}
	// returns sym without scope
	[[nodiscard]] WTString Sym() const
	{
		return GetStrs()->GetSym();
	}
	[[nodiscard]] symstrs_t::locked_string_view Sym_sv() const
	{
		return GetStrs()->GetSym_sv();
	}

	// helpers made to optimize hashtable loops by preventing use of temporaries
	[[nodiscard]] int SymScopeStartsWith(LPCTSTR str)
	{
		return GetStrs()->SymScopeStartsWith(str);
	}
	[[nodiscard]] bool DefStartsWith(LPCTSTR str)
	{
		return GetStrs(true)->DefStartsWith(str);
	}
	[[nodiscard]] bool SymMatch(uint hash2) const
	{
		// when checking by hash, assume caller has already vetted SymHash().
		// we can't check SymHash() without the string anyway.
		// this function will generally return true.
		// a return of false might be due to a collision on the primary hash.
		return mSymHash2 == hash2;
	}
	[[nodiscard]] bool SymMatch(const WTString& str) const
	{
		return SymMatch(str.c_str());
	}
	[[nodiscard]] bool SymMatch(LPCTSTR str) const
	{
		// when checking string match, assume caller has not verified SymHash().
		// checking both hashes reduces chance of collision on only a single hash.
		// this function will generally return false since it typically isn't
		// called from any of the Find* inner loops (where primary hash has
		// already been vetted).
		_ASSERTE(type() != CachedBaseclassList);
		uint hash1 = HashSym(str);
		return SymHash() == hash1 && mSymHash2 == GetSymHash2(str);
	}
	[[nodiscard]] bool IsVaForwardDeclare() const
	{
		return IsHideFromUser() && GetStrs()->SymScopeStartsWith(":ForwardDeclare");
	}

	// setters
	void SetSym(const WTString& symscope)
	{
		SetStrs(symscope, "");
	}
	void SetStrs(const WTString& symscope, const WTString& def);
	void SetDef(WTString def)
	{
		SetStrs(SymScope(), def);
	}

	// HasLocalScope() gives different results than HasLocalFlag()
	[[nodiscard]] BOOL HasLocalScope();
	[[nodiscard]] static BOOL IsLocalScope(WTString scope);

	[[nodiscard]] static uint HashSym(LPCSTR sym); // primary sym hash
	[[nodiscard]] static uint HashSym(const WTString& sym)
	{
		return HashSym(sym.c_str());
	}
	[[nodiscard]] static uint GetSymHash2(LPCSTR sym); // secondary sym hash
	[[nodiscard]] static uint GetSymHash2_sv(std::string_view sym);
	[[nodiscard]] static uint GetSymHash(const WTString& sym);
	[[nodiscard]] static uint GetScopeHash(const WTString& sym);

  private:
	[[nodiscard]] symstrs_t GetStrs(bool getBoth = false) const;
};
#pragma pack(pop)

using symstrs_t = DType::symstrs_t;

const inline size_t sizeof_reduced_DType = sizeof(DType) - sizeof(DType::sdata_rptr) - sizeof(DType::mReadWriteCount);
static_assert(sizeof(std::atomic_int) == 4);
static_assert(sizeof_reduced_DType == 32); // don't waste bytes on alignment
const inline size_t sizeof_db_entry = sizeof_reduced_DType;

#ifdef NO_REMOTE_HEAP
namespace remote_heap
{
[[nodiscard]] inline size_t remote_heap_impl::get_block_size(const sdata* ptr)
{
	assert(ptr);
	return get_block_size(ptr->get_size());
	//	return ::HeapSize(::GetProcessHeap(), 0, ptr);		// likely slow
}
} // namespace remote_heap
#endif

using DTypePtr = std::shared_ptr<DType>;

using TDTypeColl = std::list<DType>;
class DTypeList : public TDTypeColl
{
  public:
	DTypeList()
	{
	}

	void GetStrs();
	void ReloadStrs();

	void FilterNonActiveProjectDefs();
	void FilterNonActiveSystemDefs();
	void FilterDupesAndGotoDefs();
	void FilterDupes();
	void FilterGeneratedSourceFiles();

	// filter defs that are textually equivalent without regard to file or line.
	// useful for multiply declared functions.
	void FilterEquivalentDefs();

	// filter defs that are textually equivalent without regard to file or line.
	// but only for syms that originate in files we hide from user (eg, vanetobj output).
	void FilterEquivalentDefsIfNoFilePath();

	void FilterNonActive()
	{
		FilterNonActiveProjectDefs();
		FilterNonActiveSystemDefs();
	}

	// remove defs from files that do not exist, and queue a SymbolRemover run of the missing file
	void PurgeMissingFiles();
};


class ScopeHashAry
{
  public:
	static constexpr int MAX_LAYER = 60; // increased for C#


	ScopeHashAry()
	    : mDeep(-1), mUsingsStartIdx(-1)
	{
	}
	ScopeHashAry(LPCTSTR scope, const WTString pbcl, const WTString namespaces, bool splitBclForScope = false)
	{
		Init(scope, pbcl, namespaces, splitBclForScope);
	}

	void Init(const WTString& scope, const WTString pbcl, const WTString namespaces, bool splitBclForScope = false);
	int Contains(uint hsh) const;
	bool IsInited() const
	{
		return !!GetCount();
	}
	int GetCount() const
	{
		return (int)mArry.size();
	}

	uint GetHash(int i) const
	{
		assert((0 <= i) && (i < GetCount()));
		return mArry[(uint32_t)i];
	}
	int RfromI(int i) const;

  private:
	bool AddList(LPCSTR l, bool addSubScopes);
	bool AddItem(uint nHash, 
#ifdef _DEBUG
	             const WTString& tmpStr, 
#endif
	             bool force = false);

  private:
	std::vector<uint> mArry;
	phmap::flat_hash_map<uint, int> mArry_map; // <hash, i>
	phmap::flat_hash_map<uint, int> mArry_map_hcm;
#ifdef _DEBUG
	std::unordered_map<uint, WTString> mArryStrs;
#endif          // _DEBUG
	int mDeep;  // current level not including scope ":foo:bar:sym" = 2
	int mUsingsStartIdx;
};

enum FindDataFlag
{
	// no GOTODEFs, no sym guesses, no local ctors, defs concat'd, namespaces guessed, ignored files allowed
	FDF_NONE = 0,

	// allow local ctors
	FDF_CONSTRUCTOR = 0x01,

	// only return symbols that pass IS_OBJECT_TYPE (incompatible with FDF_GotoDefIsOk)
	FDF_TYPE = 0x02,

	FDF_GUESS = 0x04,

	// don't concat defs of multiple DTypes into the first found
	FDF_NoConcat = 0x08,

	// allow GOTODEF symbol type (incompatible with FDF_TYPE)
	FDF_GotoDefIsOk = 0x10,

	// don't return syms from ignored files
	FDF_SkipIgnoredFiles = 0x20,

	// this can cause increased memory and disk access because it causes
	// sym/scope load to look for potential matches
	FDF_SlowUsingNamespaceLogic = 0x40,

	// added because !FDF_GUESS doesn't prevent namespace guess logic
	FDF_NoNamespaceGuess = 0x80,

	// prevent adding namespace from a namespace guess hit to the global
	// namespace usings list.
	// flag has no effect if FDF_NoNamespaceGuess is set
	FDF_NoAddNamespaceGuess = 0x100,

	// prevent adding the global namespace list to ScopeHashAry
	FDF_IgnoreGlobalNamespace = 0x200,

	// split bcl to search for scope (don't use for members search)
	FDF_SplitBclForScope = 0x400,

	// do not check for invalid sys files
	FDF_NoInvalidSysCheck = 0x800
};

class FindData
{
	const WTString* mNamespaces;

  public:
	const WTString *sym, *scope, *baseclasslist;
	DType* record;
	int scoperank;
	int findFlag;
	ScopeHashAry scopeArray;
	FindData(const WTString* sym = NULL, const WTString* scope = NULL, const WTString* baseclasslist = NULL,
	         const WTString* namespaces = NULL, int findFlags = FDF_NONE);

	bool HasNamespaces() const
	{
		return mNamespaces && !mNamespaces->IsEmpty();
	}

	bool IsUsingNewNamespaceLogic() const
	{
		return (findFlag & FDF_SlowUsingNamespaceLogic) && HasNamespaces() && scope && (1 < scope->GetLength());
	}
};

#ifdef _DEBUG
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(0) // the default
#endif

extern volatile bool StopIt; // interrupt parsing and loading
//#define nHashAdd(c) {char tc = c;nHash = ((nHash<<7) + nHash + toupper(tc) + (tc<<(4*6)));}
// Added better support for case, so VA does not see Abba the same as abbA
// Also changed to increase speed and not call toupper
// Upper bits of hash contain case info.
#define nHashAdd(c)                                                                                                    \
	{                                                                                                                  \
		char tc = c;                                                                                                   \
		if (tc >= 'A' && tc <= 'Z')                                                                                    \
			nHash = ((nHash << 7) + nHash + (tc + ('a' - 'A')) + (((tc + nHash) & 0x7f) << (4 * 6))) |                 \
			        0x10000000; /*upper case, Added | to be sure at least one bit is set to fix "IN" and "in" having   \
			                       the same hash*/                                                                     \
		else                                                                                                           \
			nHash = ((nHash << 7) + nHash + tc); /*lower case*/                                                        \
	}

#define nHashAddW(c)                                                                                                   \
	{                                                                                                                  \
		wchar_t tc = c;                                                                                                \
		if (tc >= L'A' && tc <= L'Z')                                                                                  \
			nHash = ((nHash << 7) + nHash + (tc + (L'a' - L'A')) + (((tc + nHash) & 0x7f) << (4 * 6))) |               \
			        0x10000000; /*upper case, Added | to be sure at least one bit is set to fix "IN" and "in" having   \
			                       the same hash*/                                                                     \
		else                                                                                                           \
			nHash = ((nHash << 7) + nHash + tc); /*lower case*/                                                        \
	}

BOOL GoToDEF(LPCSTR sym);
inline BOOL GoToDEF(const WTString& sym)
{
	return GoToDEF(sym.c_str());
}
BOOL GetGoToDEFLocation(LPCSTR sym, CStringW& file, int& line, WTString& txtToSelect);
inline BOOL GetGoToDEFLocation(const WTString& sym, CStringW& file, int& line, WTString& txtToSelect)
{
	return GetGoToDEFLocation(sym.c_str(), file, line, txtToSelect);
}
CStringW GetSymbolLoc(LPCTSTR sym, int& defLineOut);
inline CStringW GetSymbolLoc(const WTString& sym, int& defLineOut)
{
	return GetSymbolLoc(sym.c_str(), defLineOut);
}
CStringW GetSymbolLocation(const DType& data);
bool GotoSymbol(DType& data);
enum GoAction
{
	Go_Default,
	Go_Definition,
	Go_Declaration
};
bool GotoSymbol(GoAction action, DType* data);
bool FindAndGotoSymbolDefinition(DType* data, bool gotoDeclIfFail = true, bool selectSymbol = true);
bool FindSymbolDefinition(DType* data, CStringW& file, int& line, WTString& sym, bool gotoDeclIfFail = true,
                          bool selectSymbol = true);
DType* FindDeclaration(DType& data);
bool AreSymbolDefsEquivalent(DType& d1, DType& d2, bool ignoreVirtualAbstractOverride = false,
                             bool ignoreUnrealPostfix = false);
WTString StripDefToBareEssentials(const WTString& s, const WTString& symName, int symType,
                                  bool ignoreVirtualAbstractOverride = false, bool ignoreUnrealPostfix = false);
WTString StripScopes(WTString txt);
DType* TraverseUsing(DType* data, MultiParse* mp, bool resolveNamespaceAlias = false);
DTypePtr TraverseUsing(DTypePtr data, MultiParse* mp, bool resolveNamespaceAlias = false);
void AdjustScopesForNamespaceUsings(MultiParse* m_mp, const WTString& bcl, WTString& baseScp, WTString& scp);

LPCSTR GetDefaultVisibilityForSnippetSubst(int devlang);
LPCSTR GetVisibilityString(SymbolVisibility v, int devLang);

DType* GetBestEnumDtype(MultiParse* pmp, DType* data);
DType* ResolveTypeStr(WTString type, DType* scopeDtype, MultiParse* mp);
void SimplifySymbolScopeForFileUsings(WTString& symType, MultiParse* mp, uint fileId);
void QualifyTypeWithFullScope(WTString& symType, DType* symDef, MultiParse* mp, const WTString& localScope,
                              const WTString& symHintScope);
WTString CombineOverlappedScope(const WTString& baseScope, const WTString& unqualifiedScope);

WTString GetSymOperatorSafe(DType* dt);
WTString GetSymScopeOperatorSafe(DType* dt);
void GetSymScopeOperatorSafe_dump(DType* dt, std::string &out);
bool IsSortedOrderLikelyNotPureChance(int ItemsInFirstList, int ItemsInSecondList = 1);
CStringW GetIncludeFromFileNameLower(CStringW text);

#endif
