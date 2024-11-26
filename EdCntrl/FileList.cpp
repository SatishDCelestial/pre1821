#include "StdAfxEd.h"
#include "FileList.h"
#include "FileId.h"
#include "SymbolPositions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

FileInfo::FileInfo(const CStringW& fname) : mFilename(fname)
{
	// mFileId is required for unique insertions and to compare items in lists
	// Not necessary if just building a list of dirs as at startup or install time
	// If it becomes necessary, may need to create FileIdManager before ComSetup
	mFileId = gFileIdManager ? gFileIdManager->GetFileId(fname) : 0;
	mFilenameLower = mFilename;
	mFilenameLower.MakeLower();
}

bool FileList::ContainsNoCase(const CStringW& str) const
{
	_ASSERTE(gFileIdManager);
	const UINT fileId = gFileIdManager->GetFileId(str);
	FileIdSet::const_iterator it2 = mFileIds.find(fileId);
	bool res = it2 != mFileIds.end();

#ifdef _DEBUG
	// test consistency between id cache and list
	const_iterator it = FindInList(fileId);
	CStringW searchValLower(str);
	searchValLower.MakeLower();
	_ASSERTE((it == end() && it2 == mFileIds.cend()) || (*it).mFilenameLower == searchValLower);
	_ASSERTE(res == (it != end()));
#endif // _DEBUG
	return res;
}

bool FileList::Contains(UINT fileId) const
{
	_ASSERTE(gFileIdManager);
	FileIdSet::const_iterator it2 = mFileIds.find(fileId);
	bool res = it2 != mFileIds.end();

#if defined(_DEBUG) && defined(VA_CPPUNIT)
	// test consistency between id cache and list
	const_iterator it = FindInList(fileId);
	_ASSERTE((it == end() && it2 == mFileIds.cend()) || (*it).mFileId == fileId);
	_ASSERTE(res == (it != end()));
#endif // _DEBUG
	return res;
}

bool FileList::Remove(const CStringW& str)
{
	_ASSERTE(gFileIdManager);
	const UINT fileId = gFileIdManager->GetFileId(str);
	FileIdSet::iterator it2 = mFileIds.find(fileId);
	if (it2 == mFileIds.end())
	{
#ifdef _DEBUG
		// test consistency between id cache and list
		iterator it = FindInList(fileId);
		_ASSERTE(it == end());
#endif
		return false;
	}

	iterator it = FindInList(fileId);
	if (it == end())
	{
		_ASSERTE(!"bad condition -- item was in mFileIds");
		return false;
	}

#ifdef _DEBUG
	CStringW searchValLower(str);
	searchValLower.MakeLower();
	_ASSERTE((*it).mFilenameLower == searchValLower);
#endif // _DEBUG
	erase(it);
	return true;
}

uint FileList::GetMemSize() const
{
	size_t sz = sizeof(*this);
	sz += mFileIds.size() * sizeof(UINT);

	for (const auto& it : *this)
	{
		sz += sizeof(it);
		sz += it.mFilename.GetLength() * sizeof(wchar_t);
		sz += it.mFilenameLower.GetLength() * sizeof(wchar_t);
	}

	return (uint)sz;
}

bool FileList::AddUniqueNoCase(const CStringW& str)
{
	_ASSERTE(gFileIdManager);
	const UINT fileId = gFileIdManager->GetFileId(str);
	return AddUniqueNoCase(str, fileId);
}
bool FileList::AddUniqueNoCase(const CStringW& str, UINT fileId)
{
	if (Contains(fileId))
		return false;

	Add(str, fileId);
	return true;
}

void FileList::AddHead(const FileList& filelist)
{
	// maintain filelist order by pushing to front in reverse order
	for (const_reverse_iterator it = filelist.rbegin(); it != filelist.rend(); ++it)
	{
		push_front(*it);
		mFileIds.insert((*it).mFileId);
	}
}

template <typename FileInfoT> void FileListT<FileInfoT>::AddHead(const FileListT& filelist)
{
	// maintain filelist order by pushing to front in reverse order
	for (typename BASE::const_reverse_iterator it = filelist.rbegin(); it != filelist.rend(); ++it)
	{
		BASE::push_front(*it);
	}
}

void ForceFileListMethodCreation()
{
	FileList zzzz;
	zzzz.ContainsNoCase(CStringW(L"foobar"));
	zzzz.Contains(1234);
	zzzz.AddUniqueNoCase(CStringW(L"foobar"));
	zzzz.Remove(CStringW(L"foobar"));
	zzzz.AddHead(zzzz);
	zzzz.AddUnique(zzzz);

	SymbolPosList xxxx;
	xxxx.Contains(1234, 1);
	xxxx.AddHead(xxxx);
}
