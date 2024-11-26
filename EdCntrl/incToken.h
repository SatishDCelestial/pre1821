#ifndef INCINCTOKENH
#define INCINCTOKENH

#include "WTString.h"
#include "DBLock.h"

class SemiColonDelimitedString;

CStringW WTExpandEnvironmentStrings(LPCWSTR str);
CStringW WTExpandEnvironmentStrings2(LPCWSTR str);
CStringW ValidateDirList(LPCWSTR dirLst);
void GetMassagedSystemDirs(SemiColonDelimitedString& incDirs);

enum class TriState
{
	uninit = 0,
	off = 1,
	on = 2
};

class IncludeDirs
{
  public:
	IncludeDirs()
	{
		Init();
	}

	static void Reset();
	static void ProjectLoaded();
	static void UpdateRegistry();
	static bool IsSolutionPrivateSysFile(const CStringW& file);
	static bool IsSystemFile(const CStringW& file);
	static bool IsSystemFile(UINT fileId);
	// IsSystemFile is implemented via IsSystemFile_CheckCache and IsSystemFile_UpdateCache.
	// Use this overload in loops for improved performance (by hoisting incDirs outside of the loop).
	static bool IsSystemFile(const CStringW& file, SemiColonDelimitedString& incDirs);
	static bool IsCustom();

	CStringW getSysIncludes() const;
	CStringW getSolutionPrivateSysIncludeDirs() const;
	CStringW getAdditionalIncludes() const;
	CStringW getImportDirs() const;
	CStringW getSourceDirs() const;
	WTString GetPlatform() const
	{
		return mPlatformKey;
	}

	void RemoveSysIncludeDir(CStringW dir);
	void RemoveSourceDir(CStringW dir);
	void AddSysIncludeDir(CStringW dir);
	void AddSolutionPrivateSysIncludeDir(CStringW dir);
	void AddSourceDir(CStringW dir, bool checkIncDirs = false);

	bool IsSetup() const;
	static void SetupPlatform(LPCSTR platform);

	void DumpToLog() const;

  private:
	void Init();
	void SetupCustom();
	static void SetupProjectPlatform();
	static void WriteDirsToRegistry();
	static TriState IsSystemFile_CheckCache(const UINT fid);
	static TriState IsSystemFile_UpdateCache(const CStringW& file, const UINT fid, SemiColonDelimitedString& incDirs);

	static CStringW m_sysInclude;
	static CStringW m_solutionPrivateSysIncludes;
	static CStringW m_additionalIncludes;
	static CStringW m_importDirs;
	static CStringW m_srcDirs;
	static WTString mPlatformKey;
	static RWLock mLock;
};

extern const WTString kCustom;
extern const WTString kProjectDefinedPlatform;

#endif
