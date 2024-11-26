#include "StdAfxEd.h"
#include "FileFinder.h"
#include "FileTypes.h"
#define FILEFINDER_CACHE
#include "FILE.H"
#include "FileId.h"
#include "Lock.h"
#include "incToken.h"
#include "Settings.h"
#include "TraceWindowFrame.h"
#include "wt_stdlib.h"
#include "FDictionary.h"
#include "PrivateHeapStringMgr.h"
#include "RegKeys.h"
#include "Registry.h"
#include "log.h"
#include "VAHashTable.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#pragma warning(disable : 4503)

const CStringW kResolutionDb("ff1.va");
const CStringW kLocalResolutionDb("ff2.va");
#define DELIM L"\247"
FileFinder* gFileFinder = nullptr;
Win32Heap gFileFinderHeapAllocator;
PrivateHeapStringMgr gFileFinderStringManager(1, "FFstrs");

void InitFileFinder()
{
	if (!gFileFinderHeapAllocator.IsCreated())
		gFileFinderHeapAllocator.Create(1 * 1024 * 1024, "FFMap");

	if (!gFileFinder)
		gFileFinder = new FileFinder;
}

void ClearFileFinder()
{
	if (gFileFinder)
	{
		auto tmp = gFileFinder;
		gFileFinder = nullptr;
		delete tmp;
	}

	gFileFinderHeapAllocator.Close();
}

FileFinder::FileFinder()
    : mModified(false)
{
}

FileFinder::~FileFinder()
{
	_ASSERTE(!mModified);
}

// Additional cache used to store 'fileToResolve' hits that were resolved not using 'relativeToDirectory' parameter (meaning, it was found within solution/project include paths).
// Accessed under mCacheLock.
// Cache will be active only during the initial parse phase after opening the project as we don't track cached file changes.
//  ** disabled as it didn't bring any gain after other optimizations have been implemented
struct UnrealCache : public dir_cache_t
{
	const std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();
	FileFinder::LocationValues mCache_noRelativeToDir;
	FileFinder::LocationValues mLocalCache_noRelativeToDir;
};

CStringW FileFinder::ResolveInclude(CStringW fileToResolve, CStringW relativeToDirectory, BOOL searchLocalPath)
{
	const LocationId locId = gFileIdManager->GetFileId(relativeToDirectory);
	const int nlPos = fileToResolve.FindOneOf(L"\r\n");

	if (-1 != nlPos)
		fileToResolve = fileToResolve.Left(nlPos);

	Table& cache = searchLocalPath ? mLocalCache : mCache;
	LocationValues* cache_noRelativeToDir = nullptr;
	{
		AutoLockCs l(mCacheLock);
		LocationValue *val = nullptr;
		
		static const bool use_unreal_cache = !GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, "NewFileFinderCacheDontUse", false);
		if (use_unreal_cache && GlobalProject && GlobalProject->IsBusy())
		{
			if(!unreal_cache)
				unreal_cache = std::make_shared<UnrealCache>();
//			cache_noRelativeToDir = searchLocalPath ? &unreal_cache->mLocalCache_noRelativeToDir : &unreal_cache->mCache_noRelativeToDir;
// 
// 			auto it = cache_noRelativeToDir->find(fileToResolve);
// 			if(it != cache_noRelativeToDir->end())
// 			{
// 				val = &it->second;
// 
// 				if(val->first)
// 					++unreal_cache->resolveinclude_norelativetodir_cache_hit_cnt;
// 			}
		}
		else if (unreal_cache)
			unreal_cache.reset();


		// orig code:
		if(!val)
		{
			LocationValues& vals = cache[locId];
			val = &vals[fileToResolve];
		}

		if (val->first)
		{
			if (val->second.IsEmpty() && -1 != val->first)
				val->second = gFileIdManager->GetFile(val->first);

			if (unreal_cache)
				++unreal_cache->resolveinclude_total_cache_hit_cnt;
			return val->second;
		}

		_ASSERTE(val->second.IsEmpty());
	}

	bool relativeToDirectory_used = true;
	const CStringW res = FindFile(fileToResolve, relativeToDirectory, searchLocalPath, unreal_cache, &relativeToDirectory_used);
	if (unreal_cache)
		++unreal_cache->resolveinclude_cache_not_hit_cnt;

	// test regkey, do not document
	static const bool test_unreal_cache = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, "NewFileFinderCacheTest", false);
	if (test_unreal_cache && unreal_cache)
	{
		bool relativeToDirectory_used2 = true;
		const CStringW res2 = FindFile(fileToResolve, relativeToDirectory, searchLocalPath, nullptr, &relativeToDirectory_used2);
		if ((relativeToDirectory_used != relativeToDirectory_used2) || (res != res2))
			++unreal_cache->findfile_test_errors_cnt;
	}

	CacheResult(cache, res, locId, fileToResolve, (!res.IsEmpty() && !relativeToDirectory_used) ? cache_noRelativeToDir : nullptr);
	return res;
}

void FileFinder::ResetUnrealCache()
{
	AutoLockCs l(mCacheLock);
	unreal_cache.reset();
}

void FileFinder::DumpStats()
{
	if(unreal_cache)
	{
		auto stop_time = std::chrono::high_resolution_clock::now();
		auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - unreal_cache->start_time).count();

		auto [dirs_count, entries_count] = dir_t::get_stats(*unreal_cache.get());

		vCatLog("Parser.FileFinder", "FileFinder stats: finished in %zums", (size_t)milliseconds);
		vCatLog("Parser.FileFinder", "FileFinder stats: dirs_count(%zu); entries_count(%zu)", dirs_count, entries_count);
		vCatLog("Parser.FileFinder", "FileFinder stats: enum_dir_failed_cnt(%llu)", unreal_cache->enum_dir_failed_cnt.load());
		vCatLog("Parser.FileFinder", "FileFinder stats: concurrent_max(%llu)", unreal_cache->enum_dir_max_concurrent_cnt.load());
		vCatLog("Parser.FileFinder", "FileFinder stats: find_dir_discarded_allocation_cnt(%llu)", unreal_cache->find_dir_discarded_allocation_cnt.load());
		vCatLog("Parser.FileFinder", "FileFinder stats: IsFile_cnt(%llu), get_itok_cnt(%llu)", unreal_cache->IsFile_cnt.load(), unreal_cache->get_itok_cnt.load());
		vCatLog("Parser.FileFinder", "FileFinder stats: findfile_fallback1_cnt(%llu), findfile_fallback2_cnt(%llu), findfile_cache_hit_cnt(%llu), findfile_isfile_avoided_cnt(%llu)", unreal_cache->findfile_fallback1_cnt.load(), unreal_cache->findfile_fallback2_cnt.load(), unreal_cache->findfile_cache_hit_cnt.load(), unreal_cache->findfile_isfile_avoided_cnt.load());
		vCatLog("Parser.FileFinder", "FileFinder stats: resolveinclude_norelativetodir_cache_hit_cnt(%llu), resolveinclude_total_cache_hit_cnt(%llu), resolveinclude_cache_not_hit_cnt(%llu)", unreal_cache->resolveinclude_norelativetodir_cache_hit_cnt.load(), unreal_cache->resolveinclude_total_cache_hit_cnt.load(), unreal_cache->resolveinclude_cache_not_hit_cnt.load());
		vCatLog("Parser.FileFinder", "FileFinder stats: mCache entries(%zu), mLocalCache entries(%zu), mCache_noRelativeToDir entries(%zu), mLocalCache_noRelativeToDir entries(%zu)", mCache.size(), mLocalCache.size(), unreal_cache->mCache_noRelativeToDir.size(), unreal_cache->mLocalCache_noRelativeToDir.size());

		static const bool test_unreal_cache = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, "NewFileFinderCacheTest", false);
		if(test_unreal_cache)
			vCatLog("Parser.FileFinder", "FileFinder stats: findfile_test_errors_cnt(%llu)", unreal_cache->findfile_test_errors_cnt.load());
	}
	else
		CatLog("Parser.FileFinder", "FileFinder: no new cache");
}
void FileFinder_ProjectLoadingStarted()
{
//	::OutputDebugStringA("FEC: FileFinder_ProjectLoadingStarted");
	CatLog("Parser.FileFinder", "FileFinder: Loading started");

	if (gFileFinder)
		gFileFinder->ResetUnrealCache();
	ClearShouldIgnoreFileCache();
}
void FileFinder_ProjectLoadingFinished()
{
	if(gFileFinder)
	{
		gFileFinder->DumpStats();
		gFileFinder->ResetUnrealCache();
	}
	ClearShouldIgnoreFileCache();

	CatLog("Parser.FileFinder", "FileFinder: Loading finished");
//	::OutputDebugStringA("FEC: FileFinder_ProjectLoadingFinished");
}


CStringW FileFinder::ResolveReference(CStringW fileToResolve, CStringW relativeToDirectory)
{
	const LocationId locId = gFileIdManager->GetFileId(relativeToDirectory);
	const int nlPos = fileToResolve.FindOneOf(L"\r\n");

	if (-1 != nlPos)
		fileToResolve = fileToResolve.Left(nlPos);

	{
		AutoLockCs l(mCacheLock);
		LocationValues& vals = mLocalCache[locId];
		// this lookup might benefit from optimization (hash?)
		LocationValue& val = vals[fileToResolve];

		if (val.first)
		{
			if (val.second.IsEmpty() && -1 != val.first)
				val.second = gFileIdManager->GetFile(val.first);

			return val.second;
		}

		_ASSERTE(val.second.IsEmpty());
	}

	const CStringW res = FindFile(fileToResolve, relativeToDirectory, true);
	CacheResult(mLocalCache, res, locId, fileToResolve);
	return res;
}

CStringW FileFinder::FindFile(const CStringW& fileToResolve, const CStringW& relativeToDirectory, bool searchLocalPath, dir_cache_ptr dc, bool* relativeToDirectory_used)
{
	CStringW res(fileToResolve);
	const int ftype = ::GetFileType(res);
	const int findTypeResult = ::FindFile(res, relativeToDirectory, searchLocalPath, ftype == Binary, dc, relativeToDirectory_used);

	if (-1 == findTypeResult)
		res.Empty();

	return res;
}

IAtlStringMgr* AFXAPI FileFinder::GetStringManager()
{
	return &gFileFinderStringManager;
}

void FileFinder::CacheResult(Table& cache, const CStringW& res, UINT locId, const CStringW& fileToResolve, LocationValues* cache_noRelativeToDir)
{
	// [case: 92495] convert from CStringW to FileFinderString for storage in map
	const FileFinderString ftr((LPCWSTR)fileToResolve, fileToResolve.GetLength(), GetStringManager());
	const FileFinderString ffs((LPCWSTR)res, res.GetLength(), GetStringManager());

	AutoLockCs l(mCacheLock);
	LocationValues& vals = cache[locId];
	LocationValue& val = vals[ftr];

	if (res.IsEmpty())
	{
		val.first = (UINT)-1;
		val.second.Empty();
	}
	else
	{
		mModified = true;
		val.second = ffs;
		val.first = gFileIdManager->GetFileId(res);
	}

	if(cache_noRelativeToDir)
		(*cache_noRelativeToDir)[ftr] = val;
}

void FileFinder::Invalidate()
{
	AutoLockCs l(mCacheLock);
	mCache.clear();
	mLocalCache.clear();
	IncludeDirs id;
	mIncDirs = id.getSysIncludes() + L";" + id.getAdditionalIncludes();
	mModified = true;
	unreal_cache.reset();
}

void FileFinder::SaveAndClear()
{
	AutoLockCs l(mCacheLock);

	if (mModified)
	{
		Save();
		mModified = false;
	}

	mDbDir.Empty();
	mIncDirs.Empty();
	mCache.clear();
	mLocalCache.clear();
	unreal_cache.reset();
}

void FileFinder::Reload()
{
	AutoLockCs l(mCacheLock);
	mCache.clear();
	mLocalCache.clear();
	Load(mDbDir);
}

// 	typedef std::pair<ResolvedId, FileFinderString> LocationValue; // path and fileId
//			(fileId : filepath)
// 	typedef std::map<FileFinderString, LocationValue> LocationValues; // map include text to located fileId
// 			includeText : (fileId : filepath)
// 	typedef std::map<LocationId, LocationValues> Table; // map relativeToDir to (include text, resolved fileId)
// 			dir1Id : [includeText1 : (fileId : filepath)]
// 			dir1Id : [includeText2 : (fileId : filepath)]
// 			dir2Id : [includeText3 : (fileId : filepath)]

class FileFinder::FinderCacheReader
{
	CStringW mFilename;
	UINT mIncDirHash;

  public:
	FinderCacheReader(const CStringW f)
        : mFilename(f)
	{
	}

	UINT GetIncDirsHash() const
	{
		return mIncDirHash;
	}

	void LoadTo(Table& tab)
	{
		if (!::IsFile(mFilename))
			return;

		FileFinderString fileContents;

		{
			CStringW fileContents2;
			for (int idx = 0; idx < 10; ++idx)
			{
				if (::ReadFileUtf16(mFilename, fileContents2))
					break;
				::Sleep(100);
			}

			const FileFinderString fileContents3((LPCWSTR)fileContents2, fileContents2.GetLength(), GetStringManager());
			fileContents = fileContents3;
		}

		// first line is inc dir hash
		int pos = fileContents.Find(L"\n");

		if (-1 == pos)
			return;

		FileFinderString tmp;
		tmp = fileContents.Mid(0, pos);
		mIncDirHash = (UINT)::wtox(tmp);
		++pos;
		UINT locId;
		FileFinderString textToResolve;
		UINT resolvedId;

		while (pos < fileContents.GetLength())
		{
			// locationId
			int nextPos = fileContents.Find(DELIM, pos);
			if (-1 == nextPos)
				break;
			tmp = fileContents.Mid(pos, nextPos - pos);
			_ASSERTE(tmp.GetLength() && tmp[tmp.GetLength() - 1] != L'\r' && tmp[tmp.GetLength() - 1] != L'\n');
			locId = (UINT)::wtox(tmp);
			pos = nextPos + 1;

			// read text field
			nextPos = fileContents.Find(DELIM, pos);
			if (-1 == nextPos)
				break;
			textToResolve = fileContents.Mid(pos, nextPos - pos);
			pos = nextPos + 1;

			// read resolvedId
			nextPos = fileContents.Find(L'\n', pos);
			if (-1 == nextPos)
				nextPos = fileContents.GetLength();
			resolvedId = (UINT)::wtox(fileContents.Mid(pos, nextPos - pos));
			pos = nextPos + 1;

			LocationValues& vals = tab[locId];
			LocationValue& val = vals[textToResolve];
			if (!resolvedId || -1 == resolvedId)
			{
				_ASSERTE(!"this shouldn't happen - bad write?");
				val.first = (UINT)-1;
				val.second.Empty();
			}
			else
			{
				val.first = resolvedId;
				// val.second set lazily
			}
		}
	}
};

class FileFinder::FinderCacheWriter
{
	CFileW mFile;
	CStringW mLn;

  public:
	FinderCacheWriter(const CStringW oFile)
	{
		for (int idx = 0; idx < 10; ++idx)
		{
			if (mFile.Open(oFile, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit))
				break;

			::Sleep(250);
		}
	}

	void WriteLine(FileFinderString& txt)
	{
		mFile.Write(txt, txt.GetLength() * sizeof(WCHAR));
		mFile.Write(L"\n", 1 * sizeof(WCHAR));
	}

	void Write(Table& tab)
	{
		std::for_each(tab.begin(), tab.end(), [this](std::pair<const LocationId, const LocationValues&> pr) {
			FileFinderString prefix;
			prefix.Format(L"%x", uint(pr.first));

			std::for_each(pr.second.begin(), pr.second.end(),
			              [this, prefix](std::pair<const FileFinderString&, const LocationValue&> pr2) {
				              if (-1 == pr2.second.first || !pr2.second.first)
					              return;

				              CString__FormatW(mLn, L"%s" DELIM L"%s" DELIM L"%x\n", (LPCWSTR)prefix,
				                               (LPCWSTR)pr2.first, pr2.second.first);
				              mFile.Write(mLn, mLn.GetLength() * sizeof(WCHAR));
			              });
		});
	}
};

void FileFinder::Load(CStringW dbDir)
{
	AutoLockCs l(mCacheLock);
	mDbDir = dbDir;
	Invalidate();
	mModified = false;

	if (mDbDir.IsEmpty() || !Psettings || !Psettings->mCacheFileFinderData)
		return;

	TimeTrace tr("FileFinder cache load");
	const UINT incDirsHash = ::WTHashKeyW(mIncDirs);

	{
		FinderCacheReader fr(mDbDir + kResolutionDb);
		fr.LoadTo(mCache);

		if (fr.GetIncDirsHash() != incDirsHash)
		{
			Invalidate();
			return;
		}
	}

	{
		FinderCacheReader fr(mDbDir + kLocalResolutionDb);
		fr.LoadTo(mLocalCache);

		if (fr.GetIncDirsHash() != incDirsHash)
		{
			Invalidate();
			return;
		}
	}
}

void FileFinder::Save()
{
	if (mDbDir.IsEmpty() || !Psettings || !Psettings->mCacheFileFinderData)
		return;

	TimeTrace tr("FileFinder cache save");
	FileFinderString tmp;
	tmp.Format(L"%x", uint(::WTHashKeyW(mIncDirs)));

	{
		FinderCacheWriter fw(mDbDir + kResolutionDb);
		fw.WriteLine(tmp);
		fw.Write(mCache);
	}

	{
		FinderCacheWriter fw(mDbDir + kLocalResolutionDb);
		fw.WriteLine(tmp);
		fw.Write(mLocalCache);
	}
}
