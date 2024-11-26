#pragma once

#include <vector>
#include "Lock.h"
#include "WTString.h"

//#define FILTER_STD_SORT_DO_TEST
//#define FILTER_STD_SORT_MEASURE

template <typename CH, typename T>
class FilterableSet
{
  protected:
	int mCapacity = 0;
	const int mInitialCapacity = 4000;
	T* mAllItems = nullptr;
	int mAllItemCount = 0;
	std::basic_string<CH> mFilter;
	T* mFilteredSet = nullptr;
	int mFilteredSetCount = 0;
	int mSuggestedItemIdx = 0;
	mutable CCriticalSection mCs;

  public:
	FilterableSet() = default;
	FilterableSet(int initialCapacity) : mInitialCapacity(initialCapacity) {}

	virtual ~FilterableSet()
	{
		Empty();
	}

	int GetCount() const
	{
		return mAllItemCount;
	}
	int GetFilteredSetCount() const
	{
		return mFilteredSetCount;
	}
	virtual int GetSuggestionIdx() const
	{
		return mSuggestedItemIdx;
	}

	T GetAt(int n)
	{
		AutoLockCs l(mCs);
		if (mFilteredSet && n >= 0 && n < mFilteredSetCount)
			return mFilteredSet[n];
		return NULL;
	}

	virtual void Sort(bool(__cdecl* sortingFunc)(T&, T&))
	{
		AutoLockCs l(mCs);
#ifdef FILTER_STD_SORT_MEASURE
		DWORD t1 = ::GetTickCount();
#endif
		if (mAllItems && mAllItemCount)
		{
#ifdef FILTER_STD_SORT_DO_TEST
			std::vector<T> mAllItems2(mAllItems, &mAllItems[mAllItemCount]);
			std::sort(mAllItems2.begin(), mAllItems2.end(), sortingFunc);
#endif
			Concurrency::parallel_buffered_sort(mAllItems, &mAllItems[mAllItemCount], sortingFunc);
#ifdef FILTER_STD_SORT_DO_TEST
			_ASSERTE(!memcmp(mAllItems, &mAllItems2[0], mAllItemCount * sizeof(T)));
#endif
		}

#ifdef FILTER_STD_SORT_MEASURE
		DWORD t2 = ::GetTickCount();
#endif
		if (mFilteredSet && mFilteredSetCount)
		{
#ifdef FILTER_STD_SORT_DO_TEST
			std::vector<T> mFilteredSet2(mFilteredSet, &mFilteredSet[mFilteredSetCount]);
			std::sort(mFilteredSet2.begin(), mFilteredSet2.end(), sortingFunc);
#endif
			Concurrency::parallel_buffered_sort(mFilteredSet, &mFilteredSet[mFilteredSetCount], sortingFunc);
#ifdef FILTER_STD_SORT_DO_TEST
			_ASSERTE(!memcmp(mFilteredSet, &mFilteredSet2[0], mFilteredSetCount * sizeof(T)));
#endif
		}

#ifdef FILTER_STD_SORT_MEASURE
		DWORD t3 = ::GetTickCount();
		char temp[512];
		sprintf_s(temp, "1. %ld items, %ldms\n2. %ld items, %ldms", mAllItemCount, t2 - t1, mFilteredSetCount, t3 - t2);
		::AfxMessageBox(temp);
#endif
	}

	void AddItem(T item)
	{
		AutoLockCs l(mCs);
		if (mAllItemCount >= mCapacity)
		{
			if (!IncreaseCapacity())
				return;
		}
		mAllItems[mAllItemCount++] = item;
	}

	// this is virtual so that it can be run from the base class destructor
	virtual void Empty()
	{
		AutoLockCs l(mCs);
		if (mAllItems)
			free(mAllItems);
		if (mFilteredSet)
			free(mFilteredSet);
		mCapacity = mAllItemCount = mFilteredSetCount = 0;
		mAllItems = mFilteredSet = NULL;
		mFilter.clear();
	}

	void RemoveDuplicates()
	{
	}
	void FilterFiles(std::basic_string_view<CH> newFilterIn, bool use_fuzzy, bool force /*= false*/, std::optional<bool> searchFullPath /*= {}*/);
	void Filter(std::basic_string_view<CH> newFilter, bool use_fuzzy, bool force = false, std::optional<bool> searchFullPath = std::nullopt);

  protected:
	virtual std::basic_string_view<CH> ItemToText(T item, std::basic_string<CH> &storage) const = 0;
	virtual bool IsItemToTextMultiThreadSafe() const
	{
		return false;
	}

	T GetOrgAt(int n)
	{
#ifdef SEAN
		_ASSERTE(mCs.m_sect.LockCount);
#endif
		if (mAllItems && n >= 0 && n < mAllItemCount)
			return mAllItems[n];
		return NULL;
	}

	bool IncreaseCapacity()
	{
#ifdef SEAN
		_ASSERTE(mCs.m_sect.LockCount);
#endif
		const int newCapacity = mCapacity ? mCapacity * 2 : mInitialCapacity;
		T* tempptr = (T*)::realloc(mAllItems, sizeof(T*) * newCapacity);
		T* tempptr2 = (T*)::realloc(mFilteredSet, sizeof(T*) * newCapacity);
		if (tempptr == NULL || tempptr2 == NULL)
		{
			if (tempptr)
				mAllItems = tempptr;
			if (tempptr2)
				mFilteredSet = tempptr2;
			return false;
		}

		mCapacity = newCapacity;
		mAllItems = tempptr;
		mFilteredSet = tempptr2;
		return true;
	}
};

typedef FilterableSet<char, LPCSTR> FilterableStringSet;

// FilteredStringList is a filterable set of DSFile entries
class FilteredStringList : public FilterableStringSet
{
	typedef std::vector<LPCSTR> StrVector;
	StrVector mAllocPool;

  public:
	FilteredStringList() : FilterableStringSet(), mSymbolTypes(stfAll)
	{
	}

	FilteredStringList(int initialCapacity) : FilterableStringSet(initialCapacity), mSymbolTypes(stfAll)
	{
	}

	virtual ~FilteredStringList();

	void ReadDB(BOOL restrictToWorkspace = true);
	void RemoveDuplicates();
	virtual void Empty();

	enum SymbolTypeFilter
	{
		stfAll,
		stfOnlyTypes
	};
	void SetSymbolTypes(SymbolTypeFilter types)
	{
		Empty();
		mSymbolTypes = types;
	}

	protected:
	virtual std::string_view ItemToText(const char *item, std::string &storage) const override
	{
		return {item, item + 1024}; // a dirty hack to avoid doing strlen; string is null-terminated and we'll check for 0 in StrMatchRankedT
	}
	virtual bool IsItemToTextMultiThreadSafe() const override
	{
		return true;
	}

  private:
	void ReadDBFile(const CStringW& dbFile);
	void ReadDBDir(const CStringW& dbDir);
	void AddItem(LPCSTR strPtr);

	SymbolTypeFilter mSymbolTypes;
};

class FilteredDTypeList : public FilterableSet<char, class DType*>
{
  public:
	FilteredDTypeList() : FilterableSet<char, class DType*>()
	{
	}

	FilteredDTypeList(int initialCapacity) : FilterableSet<char, DType*>(initialCapacity)
	{
	}

	void PreSort();
	void RemoveDuplicates();
	void PostSort(bool byType);

  protected:
	virtual std::string_view ItemToText(DType *item, std::string &storage) const override; // not sure if it's MT safe, but doesn't matter as this one is not critical
};

bool FilteredSetStringCompareNoCase(const LPCSTR& v1, const LPCSTR& v2, int partialMatchLen);

inline bool FilteredSetStringCompareNoCase(LPCSTR& v1, LPCSTR& v2)
{
	return FilteredSetStringCompareNoCase(v1, v2, 2048);
}

void GetDsItemSymTypeAndDb(LPCSTR pStr, int& symType, int& vaDb);
void GetDsItemSymLocation(LPCSTR pStr, CStringW& file, int& line);
void GetDsItemTypeAndAttrsAfterText(LPCSTR pStr, UINT& type, UINT& attrs);
