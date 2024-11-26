#pragma once

#include <map>
#include "SpinCriticalSection.h"
#include "Win32Heap.h"
#include "PrivateHeapAllocator.h"
#include "PrivateHeapStringMgr.h"
#include <type_traits>
#include <concepts>
#define FILEFINDER_CACHE
#include "FILE.H"

extern Win32Heap gFileFinderHeapAllocator;


template <typename T, typename CH>
concept DerivedFromChTraitsCRT = std::is_base_of_v<ATL::ChTraitsCRT<CH>, T>;


struct UnrealCache;

class FileFinder
{
  public:
	FileFinder();
	~FileFinder();
	CStringW ResolveInclude(CStringW fileToResolve, CStringW relativeToDirectory, BOOL searchLocalPath);
	CStringW ResolveReference(CStringW fileToResolve, CStringW relativeToDirectory);
	void Load(CStringW dbDir);
	void Reload();
	void Invalidate();
	void SaveAndClear();
	void ResetUnrealCache();

	void DumpStats();

	template <typename _CharType = wchar_t, class StringIterator = ATL::ChTraitsCRT<_CharType>>
	class FileFinderStrTrait : public StringIterator
	{
	  public:
		static HINSTANCE FindStringResourceInstance(UINT nID) throw()
		{
			return (AfxFindStringResourceHandle(nID));
		}

		static ATL::IAtlStringMgr* GetDefaultManager() throw()
		{
			return (GetStringManager());
		}
	};
	struct ffs_less
	{
		template <typename CH, DerivedFromChTraitsCRT<CH> LEFT_TRAITS, DerivedFromChTraitsCRT<CH> RIGHT_TRAITS>
		bool operator()(const ATL::CStringT<CH, LEFT_TRAITS> &left, const ATL::CStringT<CH, RIGHT_TRAITS> &right) const
		{
			return LEFT_TRAITS::StringCompare(left.GetString(), right.GetString()) < 0;
		}
	};

	using FileFinderString = ATL::CStringT<wchar_t, FileFinderStrTrait<wchar_t>>;
	using LocationId = UINT;                                       // key (a fileId)
	using ResolvedId = UINT;                                       // value (a fileId)
	using LocationValue = std::pair<ResolvedId, FileFinderString>; // path and fileId
	using LocationValueAllocator = PrivateHeapAllocator<std::pair<const FileFinderString, LocationValue>, gFileFinderHeapAllocator>;
	using LocationValues = std::map<FileFinderString, LocationValue, ffs_less, LocationValueAllocator>; // map include text to located fileId
	using TableAllocator = PrivateHeapAllocator<std::pair<const LocationId, LocationValues>, gFileFinderHeapAllocator>;
	using Table = std::map<LocationId, LocationValues, std::less<>, TableAllocator>; // map relativeToDir to (include text, resolved fileId)
	class FinderCacheWriter;
	class FinderCacheReader;


  private:
	CStringW FindFile(const CStringW& fileToResolve, const CStringW& relativeToDirectory, bool searchLocalPath, dir_cache_ptr dc = nullptr, bool* relativeToDirectory_used = nullptr);
	void CacheResult(Table& cache, const CStringW& res, UINT locId, const CStringW& fileToResolve, LocationValues* cache_noRelativeToDir = nullptr);
	void Save();
	static IAtlStringMgr* AFXAPI GetStringManager();


	Table mCache;                            // system includes
	Table mLocalCache;                       // all other includes
	mutable CSpinCriticalSection mCacheLock; // for both Tables
	CStringW mDbDir;
	CStringW mIncDirs;
	bool mModified;

	std::shared_ptr<UnrealCache> unreal_cache;
};

extern FileFinder* gFileFinder;

void InitFileFinder();
void ClearFileFinder();
void FileFinder_ProjectLoadingStarted();
void FileFinder_ProjectLoadingFinished();
