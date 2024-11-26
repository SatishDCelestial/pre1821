#pragma once

#include <list>
#include <set>
#include "FileId.h"

// FileInfo
// ----------------------------------------------------------------------------
// Base struct for FileInfo
// Derive from it if you need to add positioning info, etc
//
struct FileInfo
{
	FileInfo() : mFileId(0)
	{
	}

	FileInfo(const FileInfo& rhs) : mFilename(rhs.mFilename), mFilenameLower(rhs.mFilenameLower), mFileId(rhs.mFileId)
	{
		if (mFilenameLower.IsEmpty())
		{
			_ASSERTE(!"FileInfo lower not set");
			mFilenameLower.MakeLower();
		}
	}
	FileInfo& operator=(const FileInfo&) = default;

	FileInfo(const CStringW& fname);

	FileInfo(const CStringW& fname, UINT fileId) : mFilename(fname), mFileId(fileId)
	{
		mFilenameLower = mFilename;
		mFilenameLower.MakeLower();
	}

	FileInfo(const CStringW& fname, const CStringW& lowerFname, UINT fileId)
	    : mFilename(fname), mFilenameLower(lowerFname), mFileId(fileId)
	{
	}

	bool operator==(const FileInfo& rhs) const
	{
		return mFileId == rhs.mFileId;
	}

	CStringW mFilename;
	CStringW mFilenameLower;
	UINT mFileId;
};

// FileListT
// ----------------------------------------------------------------------------
// List of FileInfoTs where FileInfoT is derived from FileInfo.
// Supports multiple entries for same file (unique by derived class, for example line number).
//
template <typename FileInfoT> class FileListT : protected std::list<FileInfoT>
{
  public:
	using BASE = std::list<FileInfoT>;
	using iterator = typename std::list<FileInfoT>::iterator;
	using const_iterator = typename std::list<FileInfoT>::const_iterator;

	FileListT() : std::list<FileInfoT>()
	{
	}

	void Add(const FileInfoT& fi)
	{
		BASE::push_back(fi);
	}

	void Add(const FileListT& filelist)
	{
		for (const_iterator it = filelist.begin(); it != filelist.end(); ++it)
		{
			BASE::push_back(*it);
		}
	}

	void AddHead(const FileInfoT& fi)
	{
		BASE::push_front(fi);
	}

	void AddHead(const FileListT& filelist);

  public:
	// forwards to private impl
	//	void clear() noexcept
	//	{
	//		std::list<FileInfoT>::clear();
	//	}
	using BASE::clear;

	//	iterator erase(const_iterator _Where)
	//	{
	//		return std::list<FileInfoT>::erase(_Where);
	//	}
	using BASE::erase;

	//	void remove(const FileInfoT& _Val)
	//	{
	//		std::list<FileInfoT>::remove(_Val);
	//	}
	using BASE::remove;

	//	size_type size() const noexcept
	//	{
	//		return std::list<FileInfoT>::size();
	//	}
	using BASE::size;

	//	bool empty() const noexcept
	//	{
	//		return std::list<FileInfoT>::empty();
	//	}
	using BASE::empty;

	//	iterator begin() noexcept
	//	{
	//		return std::list<FileInfoT>::begin();
	//	}
	using BASE::begin;

	//	const_iterator begin() const noexcept
	//	{
	//		return std::list<FileInfoT>::begin();
	//	}

	//	iterator end() noexcept
	//	{
	//		return std::list<FileInfoT>::end();
	//	}
	using BASE::end;

	//	const_iterator end() const noexcept
	//	{
	//		return std::list<FileInfoT>::end();
	//	}

	void swap(FileListT& _Right) noexcept
	{
		BASE::swap(_Right);
	}
	//	using std::list<FileInfoT>::swap;

	//	template<class _Pr2>
	//	void sort(_Pr2 _Pred)
	//	{
	//		return std::list<FileInfoT>::sort(_Pred);
	//	}
	using BASE::sort;
};

// FileList
// ----------------------------------------------------------------------------
//
class FileList : protected std::list<FileInfo>
{
	using FileIdSet = std::set<UINT>;
	FileIdSet mFileIds;

  public:
	using iterator = std::list<FileInfo>::iterator;
	using const_iterator = std::list<FileInfo>::const_iterator;

	FileList() : std::list<FileInfo>()
	{
	}

	bool ContainsNoCase(const CStringW& str) const;

	bool Contains(UINT fileId) const;

	void Add(const FileInfo& fi)
	{
		push_back(fi);
		mFileIds.insert(fi.mFileId);
	}

	void Add(const FileList& filelist)
	{
		for (const_iterator it = filelist.begin(); it != filelist.end(); ++it)
		{
			push_back(*it);
			mFileIds.insert((*it).mFileId);
		}
	}

	void Add(const CStringW& str)
	{
		Add(FileInfo(str));
	}

	void Add(const CStringW& str, UINT fileId)
	{
		Add(FileInfo(str, fileId));
	}

	void AddHead(const CStringW& str)
	{
		AddHead(FileInfo(str));
	}

	void AddHead(const FileInfo& fi)
	{
		push_front(fi);
		mFileIds.insert(fi.mFileId);
	}

	void AddHead(const FileList& filelist);

	bool AddUnique(const FileInfo& fi)
	{
		if (Contains(fi.mFileId))
			return false;

		Add(fi);
		return true;
	}

	bool AddUniqueNoCase(const CStringW& str);
	bool AddUniqueNoCase(const CStringW& str, UINT fileId);

	void AddUnique(const FileList& filelist)
	{
		// maintain filelist order by pushing to front in reverse order
		for (const_reverse_iterator it = filelist.rbegin(); it != filelist.rend(); ++it)
		{
			AddUnique(*it);
		}
	}

	bool operator==(const FileList& rhs) const
	{
		if (size() != rhs.size())
			return false;

		{
			FileList::const_iterator it1 = begin();
			FileList::const_iterator it2 = rhs.begin();
			for (; it1 != end(); ++it1, ++it2)
				if (!(*it1 == *it2))
					return false;
		}

		if (mFileIds.size() != rhs.mFileIds.size())
		{
			_ASSERTE(!"bad because list already matched ok");
			return false;
		}

		{
			_ASSERTE(gFileIdManager);
			FileIdSet::iterator it1 = mFileIds.begin();
			FileIdSet::iterator it2 = rhs.mFileIds.begin();
			for (; it1 != mFileIds.end(); ++it1, ++it2)
			{
				if (!(*it1 == *it2))
				{
					_ASSERTE(!"bad because list already matched ok");
					return false;
				}
			}
		}

		return true;
	}

	bool operator!=(const FileList& rhs) const
	{
		return !(*this == rhs);
	}

	bool Remove(const CStringW& str);

	uint GetMemSize() const;

  private:
	const_iterator FindInList(UINT fileId) const
	{
		_ASSERTE(fileId);
		for (const_iterator it = begin(); it != end(); ++it)
		{
			_ASSERTE((*it).mFileId);
			if ((*it).mFileId == fileId)
				return it;
		}

		return end();
	}

	iterator FindInList(UINT fileId)
	{
		_ASSERTE(fileId);
		for (iterator it = begin(); it != end(); ++it)
		{
			_ASSERTE((*it).mFileId);
			if ((*it).mFileId == fileId)
				return it;
		}

		return end();
	}

  public:
	// forwards to private impl
	void clear() noexcept
	{
		mFileIds.clear();
		__super::clear();
	}

	iterator erase(const_iterator _Where)
	{
		mFileIds.erase((*_Where).mFileId);
		return __super::erase(_Where);
	}

	void remove(const FileInfo& _Val)
	{
		mFileIds.erase(_Val.mFileId);
		__super::remove(_Val);
	}

	size_type size() const noexcept
	{
		_ASSERTE(mFileIds.size() == __super::size() || (mFileIds.size() <= 1 && !gFileIdManager));
		return __super::size();
	}

	bool empty() const noexcept
	{
		_ASSERTE(mFileIds.empty() == __super::empty() || (mFileIds.size() <= 1 && !gFileIdManager));
		return __super::empty();
	}

	reference back()
	{
		return std::list<FileInfo>::back();
	}

	iterator begin() noexcept
	{
		return std::list<FileInfo>::begin();
	}

	const_iterator begin() const noexcept
	{
		return std::list<FileInfo>::begin();
	}

	iterator end() noexcept
	{
		return std::list<FileInfo>::end();
	}

	const_iterator end() const noexcept
	{
		return std::list<FileInfo>::end();
	}

	const_iterator cbegin() const noexcept
	{
		return std::list<FileInfo>::cbegin();
	}

	const_iterator cend() const noexcept
	{
		return std::list<FileInfo>::cend();
	}

	void swap(FileList& _Right) noexcept
	{
		mFileIds.swap(_Right.mFileIds);
		__super::swap(_Right);
	}

	template <class _Pr2> void sort(_Pr2 _Pred)
	{
		return __super::sort(_Pred);
	}
};
