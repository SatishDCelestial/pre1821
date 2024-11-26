#pragma once

#include "FileList.h"
#include <set>

class ScanDirsForFiles
{
  public:
	virtual ~ScanDirsForFiles()
	{
	}

	FileList& GetFoundFiles()
	{
		return mFiles;
	}

  protected:
	ScanDirsForFiles()
	{
	}
	void BuildListOfFiles();
	void SearchDir(CStringW searchDir);

	CStringW mExtensionsList;
	std::vector<CStringW> mDirs;
	std::set<unsigned int> mSearchedDirs;
	FileList mFiles;
	bool mRecurseDirs = false;
	bool mCollectFiles = true;
	bool mConfirmUniqueAdd = true;
	bool mSkipSomeBoostDirs = true;
	int* mCancellationMonitor = nullptr;
};

class ScanSystemSourceDirs : public ScanDirsForFiles
{
  public:
	ScanSystemSourceDirs();

  protected:
	void ParseFiles();
};

class ScanSolutionPrivateSystemHeaderDirs : public ScanDirsForFiles
{
  public:
	ScanSolutionPrivateSystemHeaderDirs(const FileList& dirs);

	bool ParseFiles();

  private:
	ScanSolutionPrivateSystemHeaderDirs();
};

// [case: 1029] for Psettings->mOfisIncludeSystem support
class SimpleRecursiveFileScan : public ScanDirsForFiles
{
  public:
	SimpleRecursiveFileScan();
};

class RecursiveFileCollection : public ScanDirsForFiles
{
  public:
	RecursiveFileCollection(const FileList& dirs, bool skipBoostDirs = true, bool confirmUniqueAdd = true,
	                        int* cancelMonitor = nullptr);

  private:
	RecursiveFileCollection();
};
