#pragma once

#include "OWLDefs.h"
#include "WTString.h"
#include "..\vaIPC\vaIPC\common\string_utils.h"

#define HashStr(s) utos(WTString(s).hash())

CStringW Pwd();
CStringW MSPath(const CStringW& path);
CStringW NormalizeFilepath(LPCWSTR file);
void FixFileCase(CStringW& path);
CStringW BuildRelativePath(CStringW toFile, CStringW relativeToDir);
bool BuildRelativePathEx(CStringW& result, CStringW toFile, CStringW relativeToDir);
void Append(const CStringW& from, const CStringW& to);
WTString ReadFile(const CStringW& file);
bool ReadFileW(const CStringW& filename, CStringW& contents, int maxAmt = -1);
bool ReadFileUtf16(const CStringW& filename, CStringW& contents);

WTString Basename(const WTString& file);
CStringW Basename(const CStringW& file);
CStringW Path(const CStringW& file);

CStringW VaGetOpenFileName(HWND owner, LPCWSTR title, LPCWSTR initDir, LPCWSTR filter, int filterIndex = 1,
                           DWORD ofn_flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ENABLESIZING);

bool IsValidFileName(const CStringW& fileName);
bool IsValidPath(const CStringW& path);
CStringW TrimForAst(const CStringW& path, int endFragmetsToPreserve = 2, LPCWSTR prefix = L"TRIMMED FOR AST");

bool IsFile(LPCTSTR file);
bool IsFile(const CStringW& file);
bool IsFileReadOnly(const CStringW& file);
bool HasUtf8Bom(const CStringW& file);
bool IsDir(LPCWSTR dir);
bool CreateDir(const CStringW& dir);

bool SetFTime(const CStringW& file, FILETIME* ft);
bool SetFTime(const CStringW& file, const CStringW& org);
bool TouchFile(const CStringW& file);

BOOL WINAPI WTGetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime,
                          LPFILETIME lpLastWriteTime);
bool GetFTime(const CStringW& file, FILETIME* ft);
bool FileTimesAreEqual(const CStringW& f1, const CStringW& f2);
bool FileTimesAreEqual(const FILETIME* ft1, const CStringW& f2);

inline bool operator==(const FILETIME& ft1, const FILETIME& ft2)
{
	if ((ft1.dwHighDateTime || ft1.dwLowDateTime) && ft1.dwHighDateTime == ft2.dwHighDateTime &&
	    ft1.dwLowDateTime == ft2.dwLowDateTime)
		return true;
	return false;
}

inline bool FileTimesAreEqual(const FILETIME* ft1, const FILETIME* ft2)
{
	return *ft1 == *ft2;
}

void DuplicateFile(const CStringW& from, const CStringW& to);
bool FilesAreIdentical(const CStringW& f1, const CStringW& f2);
bool Copy(const CStringW& from, const CStringW& to, bool SaveTime = false);
BOOL IsWriteable(const CStringW& file);
int CleanDir(CStringW searchDir, LPCWSTR spec);
struct dir_cache_t;
using dir_cache_ptr = std::shared_ptr<dir_cache_t>;
int FindFile(CStringW& fileName, const CStringW& extraPath, bool searchLocalPaths, bool searchBinaryPaths, dir_cache_ptr dc = nullptr, bool* extraPath_used = nullptr);
class FileList;
int FindFiles(CStringW searchDir, LPCWSTR spec, FileList& files, BOOL includeDirectories = FALSE,
              BOOL hideDotPrefixedItems = FALSE, BOOL recurse = FALSE);
CStringW BuildTextDirList(CStringW searchDir, BOOL recurse = TRUE, BOOL includeAllFiles = TRUE);
bool SwapExtension(CStringW& fileName, bool searchSysSrcDirs = false);
void GetBestFileMatch(CStringW fname, FileList& possibleMatches);
int GetRelatedProjectSourceFiles(const CStringW& filenameToMatch, FileList& matches, bool allowPartialMatch);
DWORD GetFSize(const CStringW& file);
void SetReadWrite(const CStringW& file);
LPCWSTR GetBaseName(LPCWSTR file);
LPCWSTR GetBaseNameExt(LPCWSTR file);
CStringW GetBaseNameNoExt(LPCWSTR file);
CStringW GetCompoundExt(const CStringW& f);
void LoadAlternateFilesForGoToDef(const CStringW& filepath);
void MyRmDir(CStringW folder, bool recurse = false);
int RecycleFile(const CStringW& TargetFile);
CStringW FindModifiedFiles(const CStringW& searchDir, const CStringW& spec, FILETIME* since);
bool HasIgnoredFileExtension(const CStringW& filename);
bool ShouldIgnoreFile(const CStringW& filename, bool tmpDirIsOk);
class DefObj;
bool ShouldIgnoreFileCached(const DefObj &sym, bool tmpDirIsOk);
void ClearShouldIgnoreFileCache();
bool ShouldFileBeAttachedTo(const CStringW& filename, bool checkSize = true);
bool IsRestrictedFileType(int ftype);
BOOL IsFileTimeNewer(FILETIME* ft1, FILETIME* ft2);
CStringW GetTempFile(const CStringW& file, BOOL unicodeTarget = FALSE);
void RemoveTempFile(const CStringW& file);
CStringW AutoSaveFile(CStringW file);
void CheckAutoSaveHistory();
CStringW GetFileByType(const CStringW& file, int type);
CStringW BuildIncludeDirective(CStringW headerFileToInclude, CStringW activeFile, WCHAR pathDelimiter,
                               DWORD addIncludePath, BOOL sysOverride = FALSE);
bool IsCfile(const CStringW& fName);
CStringW GetFrameworkDirs();
CStringW GetTempDir();
bool IsUEIgnorePluginsDir(const CStringW& filename);

// Does path fragment begin with \ or / (but not \\ or //)
inline bool IsPathRootAbsolute(const CStringW& frag)
{
	if (frag.GetLength() >= 1 && (L'\\' == frag[0] || L'/' == frag[0]) && L'\\' != frag[1] && L'/' != frag[1])
		return true;
	return false;
}

inline bool IsPathAbsolute(const CStringW& path)
{
	return (path.GetLength() >= 2 && path[1] == ':') || (path.GetLength() >= 1 && (path[0] == '\\' || path[0] == '/'));
}

CStringW BuildPath(const CStringW& fragment, const CStringW& relativeToDir,
                   bool forFile = true,    // file or directory
                   bool mustExist = true); // will return empty string if non-existent and mustExist is true

bool HashFile(const CStringW& szFilename, DWORD* outHashVal);
bool HashFile(const CStringW& szFilename, BYTE outHash[20]);
bool HashText(const WTString& text, BYTE outHash[20]);
bool GetFileHashStr(const CStringW& filePath, CStringW& outHashStr);
bool GetFileLineSortedHashStr(const CStringW& filePath, CStringW& outHashStr);
WTString GetFileHashStr(const CStringW& filePath);
WTString GetTextHashStr(const WTString& text);

bool DoesFilenameCaseDiffer(const CStringW& first, const CStringW& second);



#ifdef FILEFINDER_CACHE
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <functional>
#include <tuple>
#include <vector>
#include <semaphore>
#include <shared_mutex>
#include "PrivateHeapAllocator.h"
#include "PrivateHeapStringMgr.h"
#include "Win32Heap.h"

enum class dir_exists_t : uint8_t
{
	no,
	yes,
	error
};

struct dir_t;
struct dir_entry_t
{
	dir_entry_t(bool is_dir) : is_dir(is_dir) {}

	bool is_dir = false;
	mutable std::atomic<dir_t*> dir = nullptr; // optimization; may be null
};

// **moved to WTString.h
// template<typename CH, typename TRAITS>
// struct std::hash<CStringT<CH, TRAITS>>
// {
// 	std::size_t operator()(const CStringT<CH, TRAITS>& s) const noexcept
// 	{
// //		return std::hash<std::basic_string_view<CH>>{}(std::basic_string_view<CH>{s.GetString(), s.GetString() + s.GetLength()});
// 		return (std::size_t)XXH64(s.GetString(), (uint32_t)(s.GetLength() * sizeof(CH)), 0);
// 	}
// };
template<typename CH, typename TRAITS>
struct std::hash<std::pair<size_t, CStringT<CH, TRAITS>>>
{
	std::size_t operator()(const std::pair<size_t, CStringT<CH, TRAITS>> &s) const noexcept
	{
		return s.first;
	}
};

template <typename CH, class CH_TRAITS = ATL::ChTraitsCRT<CH>>
class FFStrTraits : public CH_TRAITS
{
public:
	static HINSTANCE FindStringResourceInstance(UINT nID) throw()
	{
		return ::AfxFindStringResourceHandle(nID);
	}

	static ATL::IAtlStringMgr* GetDefaultManager() throw()
	{
		extern PrivateHeapStringMgr gFileFinderStringManager;
		return &gFileFinderStringManager;
	}
};
extern Win32Heap gFileFinderHeapAllocator;
constinit const size_t ff_unordered_map_init_bucket_count = 128 * 1024;
template <typename KEY, typename VALUE, typename HASHER = std::hash<KEY>, typename KEY_EQ = std::equal_to<KEY>>
using ff_unordered_map = std::unordered_map<KEY, VALUE, HASHER, KEY_EQ, PrivateHeapAllocator<std::pair<const KEY, VALUE>, gFileFinderHeapAllocator>>;
template <typename VALUE>
using ff_vector = std::vector<VALUE, PrivateHeapAllocator<VALUE, gFileFinderHeapAllocator>>;
template<typename CH>
using FFStringT = ATL::CStringT<CH, FFStrTraits<CH>>;
using FFString = FFStringT<char>;
using FFStringW = FFStringT<wchar_t>;

template <typename CH>
inline FFStringT<CH> to_FFString(std::basic_string_view<CH> sv)
{
	return {sv.data(), (int)sv.length()};
}

using dir_ptr = std::shared_ptr<dir_t>;
using dir_cptr = std::shared_ptr<const dir_t>;
using dirs_t = ff_unordered_map<std::pair<size_t, FFStringW>, dir_ptr>;
struct dir_cache_stats_t
{
	std::atomic<uint64_t> enum_dir_failed_cnt{0};
	std::atomic<uint64_t> enum_dir_concurrent_cnt{0};
	std::atomic<uint64_t> enum_dir_max_concurrent_cnt{0};
	std::atomic<uint64_t> find_dir_discarded_allocation_cnt{0};
	std::atomic<uint64_t> IsFile_cnt{0};
	std::atomic<uint64_t> get_itok_cnt{0};
	std::atomic<uint64_t> findfile_fallback1_cnt{0};
	std::atomic<uint64_t> findfile_fallback2_cnt{0};
	std::atomic<uint64_t> findfile_cache_hit_cnt{0};
	std::atomic<uint64_t> findfile_isfile_avoided_cnt{0};
	std::atomic<uint64_t> resolveinclude_norelativetodir_cache_hit_cnt{0};
	std::atomic<uint64_t> resolveinclude_total_cache_hit_cnt{0};
	std::atomic<uint64_t> resolveinclude_cache_not_hit_cnt{0};
	std::atomic<uint64_t> findfile_test_errors_cnt{0};
};
using dir_cache_stats_ptr = std::shared_ptr<dir_cache_stats_t>;
struct dir_cache_t : public dir_cache_stats_t
{
	// cached enumerated filenames within folders
	std::shared_mutex dirs_lock;
	dirs_t dirs{ff_unordered_map_init_bucket_count};

	dir_ptr find_dir(FFStringW path, bool already_normalized = false);
	dir_ptr follow_dir(dir_cptr current_dir, FFStringW dirname, bool already_normalized = false);


	// cached 'itok', an array of solution and project include paths to check (11k entries in UE5)
	using itok_t = ff_vector<std::tuple<FFStringW, FFStringW>>; // <normalized path, original path>
	using cached_itok_t = std::tuple<std::once_flag, itok_t>;
	std::array<cached_itok_t, 8> cached_itoks;

	cached_itok_t& get_cached_itok(bool searchBinaryPaths, bool searchLocalPaths, bool addAddlProjDirs)
	{
		return cached_itoks[(searchBinaryPaths ? 1u : 0u) + (searchLocalPaths ? 2u : 0u) + (addAddlProjDirs ? 4u : 0u)];
	}
};
using dir_cache_ptr = std::shared_ptr<dir_cache_t>;

struct dir_t : public std::enable_shared_from_this<dir_t>
{
	std::once_flag init_flag;
	dir_exists_t exists = dir_exists_t::error;
	FFStringW path; // CString has shared data, so ok; normalized
	ff_unordered_map<FFStringW, dir_entry_t> entries; // TODO: switch to std::flatmap when available!


	using FFStackStringW = CFixedStringT<FFStringW, MAX_PATH>;


	bool contains_file(FFStringW filename, bool already_normalized = false) const
	{
		assert(exists == dir_exists_t::yes);
		if (exists != dir_exists_t::yes)
			return false;

		if(!already_normalized)
			normalize_filename(filename);
		else
			assert(is_filename_normalized(filename));

		auto it = entries.find(filename);
		return (it != entries.end()) ? !it->second.is_dir : false;
	}

	dir_ptr follow_dir(dir_cache_t &dc, FFStringW dirname, bool already_normalized = false) const
	{
		assert(exists == dir_exists_t::yes);
		if (exists != dir_exists_t::yes)
			return {};

		if (dirname == L".")
			return const_cast<dir_t *>(this)->shared_from_this();
		else if(dirname == L"..")
		{
			if(path.IsEmpty())
				return {};

			std::wstring_view sv = to_string_view(path);
			auto i = sv.substr(0, sv.size() - 1).find_last_of(L'\\');
			if (i == std::wstring_view::npos)
				return {};

			if ((sv.substr(i) == L"\\.\\") || (sv.substr(i) == L"\\..\\"))
				return dc.find_dir(path + L"..\\", true);
			else
				return dc.find_dir(path.Left(int(i + 1)), true);
		}

		if(!already_normalized)
			normalize_filename(dirname);
		else
			assert(is_filename_normalized(dirname));

		auto it = entries.find(dirname);
		if (it == entries.end())
			return {};

		const dir_entry_t& de = it->second;
		if (!de.is_dir)
			return {};

		dir_t *dret = de.dir.load();
		if (dret)
			return dret->shared_from_this();

		dir_ptr ret = dc.find_dir(path + L'\\' + dirname);
		assert(ret);
		if(ret)
			de.dir = ret.get();
		return ret;
	}


protected:
	void init(const FFStringW& p, dir_cache_stats_t &dcs)
	{
		// typically max concurrent dir enumeration count seems to be around 4
		// the limit is hardcoded at 8 just in case someone doesn't use SSD
		static std::counting_semaphore max_concurrent_enums{std::min(ptrdiff_t(8), std::counting_semaphore<>::max())};

		std::call_once(init_flag, [&] {
			assert(is_path_normalized(p)); // should be already normalized
			path = p;

			{
				uint64_t concurrent_cnt = ++dcs.enum_dir_concurrent_cnt;
				uint64_t current_concurrent_cnt_max = dcs.enum_dir_max_concurrent_cnt.load();
				uint64_t concurrent_cnt_max = std::max(concurrent_cnt, current_concurrent_cnt_max);
				if (concurrent_cnt_max > current_concurrent_cnt_max)
					dcs.enum_dir_max_concurrent_cnt.compare_exchange_weak(current_concurrent_cnt_max, concurrent_cnt_max);
			}
			max_concurrent_enums.acquire();

			WIN32_FIND_DATAW fd;
			HANDLE h = ::FindFirstFileExW(FFStackStringW(path) + L'*', FindExInfoStandard, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
			if (h == INVALID_HANDLE_VALUE)
			{
				if (ERROR_PATH_NOT_FOUND == ::GetLastError())
					exists = dir_exists_t::no;

				--dcs.enum_dir_concurrent_cnt;
				max_concurrent_enums.release();
				return;
			}

			do
			{
				if (!fd.cFileName[0] || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
					continue;

				FFStringW filename(fd.cFileName);
				normalize_filename(filename);
				entries.emplace(filename, !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
			} while (::FindNextFileW(h, &fd));

			auto err = ::GetLastError();
			if (ERROR_NO_MORE_FILES == err)
				exists = dir_exists_t::yes;
			else
			{
				assert(!"Dir reading error!");
				++dcs.enum_dir_failed_cnt;
			}
			::FindClose(h);

			--dcs.enum_dir_concurrent_cnt;
			max_concurrent_enums.release();
		});
	}


public:
	static dir_ptr find_dir(dir_cache_t &dc, FFStringW path, bool already_normalized = false)
	{
		assert(!path.IsEmpty());
		if(path.IsEmpty())
			return nullptr;

		if(!already_normalized)
			normalize_path(path);
		else
			assert(is_path_normalized(path));

		dir_ptr ret;
		auto key = std::make_pair(std::hash<FFStringW>()(path), path);
		{
			std::shared_lock l(dc.dirs_lock);
			auto it = dc.dirs.find(key);
			if (it != dc.dirs.end())
				ret = it->second;
		}
		if (!ret)
		{
			ret = std::make_shared<dir_t>(); // keep allocation out of lock, even if it will be discarded
			std::unique_lock l2(dc.dirs_lock);
			auto it = dc.dirs.find(key);
			if (it != dc.dirs.end())
			{
				ret = it->second; // someone added in meanwhile
				++dc.find_dir_discarded_allocation_cnt;
			}
			else
				dc.dirs[key] = ret;
		}
		ret->init(path, dc);
		return ret;
	}

	static void normalize_path(FFStringW &path)
	{
		// paths will be lower-case, single backslashes, backslash at the at, no backslash at the front
		if(path.IsEmpty())
			return;

		int i = 0;
		if(is_path_normalized(path, &i))
			return; // exit if there is nothing to do before making a buffer copy

		if(i != -1)
		{
			wchar_t* p = path.LockBuffer();
			std::for_each(p + i, p + path.GetLength(), [](wchar_t& ch) {
				if (ch == L'/')
					ch = L'\\';
				else if ((ch >= L'A') && (ch <= L'Z'))
					ch -= wchar_t(L'A' - L'a');
			});
			path.UnlockBuffer();
		}
		path.Replace(L"\\\\", L"\\");
		if (path[path.GetLength() - 1] != L'\\')
			path += L'\\';
		if (path[0] == L'\\')
			path.Delete(0);

		assert(is_path_normalized(path));
	}
	static FFStringW normalize_path_c(FFStringW path)
	{
		normalize_path(path);
		return path;
	}
	static bool is_path_normalized(const FFStringW &path, int *out_i = nullptr)
	{
		int i = path.FindOneOf(L"/ABCDEFGHIJKLMNOPQRSTUVWXYZ");
		if(out_i)
			*out_i = i;
		if(i != -1)
			return false;

		bool has_ending_separator = path[path.GetLength() - 1] == L'\\';
		bool has_starting_separator = path[0] == L'\\';
		return has_ending_separator && !has_starting_separator && (path.Find(L"\\\\") == -1);
	}

	static void normalize_filename(FFStringW& filename)
	{
		// filenames will be lower-case
		if (filename.IsEmpty())
			return;

		int i = 0;
		if (is_filename_normalized(filename, &i))
			return; // exit if there is nothing to do before making a buffer copy

		if (i != -1)
		{
			wchar_t* p = filename.LockBuffer();
			std::for_each(p + i, p + filename.GetLength(), [](wchar_t& ch) {
				if ((ch >= L'A') && (ch <= L'Z'))
					ch -= wchar_t(L'A' - L'a');
			});
			filename.UnlockBuffer();
		}

		assert(is_filename_normalized(filename));
	}
	static FFStringW normalize_filename_c(FFStringW filename)
	{
		normalize_filename(filename);
		return filename;
	}
	static bool is_filename_normalized(const FFStringW& filename, int* out_i = nullptr)
	{
		int i = filename.FindOneOf(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ");
		if (out_i)
			*out_i = i;
		return i == -1;
	}

	// returns (mapped dirs count, total entries count)
	static std::tuple<size_t, size_t> get_stats(dir_cache_t& dc)
	{
		size_t entries_cnt = 0;

		std::shared_lock l(dc.dirs_lock);
		for(const auto &d : dc.dirs)
			entries_cnt += d.second->entries.size();

		return {dc.dirs.size(), entries_cnt};
	}
};

inline dir_ptr dir_cache_t::find_dir(FFStringW path, bool already_normalized)
{
	return dir_t::find_dir(*this, path, already_normalized);
}
inline dir_ptr dir_cache_t::follow_dir(dir_cptr current_dir, FFStringW dirname, bool already_normalized)
{
	return current_dir->follow_dir(*this, dirname, already_normalized);
}


#endif

