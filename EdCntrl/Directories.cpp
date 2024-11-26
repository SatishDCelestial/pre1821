#include "StdAfxEd.h"
#include "Directories.h"
#include "WTFStream.h"
#include "Settings.h"
#include "DevShellAttributes.h"
#include "file.h"
#include "FileTypes.h"
#include "project.h"
#include "token.h"
#include "RegKeys.h"
#include "Registry.h"
#include "wt_stdlib.h"
#include "FileId.h"
#include "resource.h"
#include "StatusWnd.h"
#include "AutotextManager.h"
#include "fdictionary.h"
#include "CFileW.h"
#include "FileList.h"
#include "BuildInfo.h"
#include "DevShellService.h"
#include "EDCNT.H"
#include "RadStudioPlugin.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MSC_VER > 1200
using std::ios;
using std::ofstream;
#endif

static CStringW sAltDir;
static LPCWSTR kVersionFilename = L"version.dat";
static const int kMaxDirAttemptsForCleanup = 50;

static void MoveUserDataToUserDir();
static CString GetCurrentVersion();
static CStringW GetAndCreateVaSpecialFolder(int specialFolderId);
static CStringW GetAppDataRoamingDir();
static CStringW GetAppDataLocalDir();
static void RemoveDbProjectDirs(const CStringW& baseDir, LPCWSTR searchSpec = L"Proj*");
static void RemoveDbDir(const CStringW& dbDir);
static void CheckForAutotextUpdate(bool isNewInstall);
static void SetupAutotextDir(bool promptUser);
static bool IsNewInstallation();
static void ConvertAutotextFilesToUtf8();

// DO NOT call any functions that will cause logging to occur during GetDllDir
static CStringW _GetDllDir()
{
	const int kAppPathLen = MAX_PATH * 2;
	WCHAR szAppPath[kAppPathLen + 1];
	WCHAR szDrive[MAX_PATH];
	WCHAR szDir[MAX_PATH];
	WCHAR szFname[MAX_PATH];
	WCHAR szExt[MAX_PATH];

	::GetModuleFileNameW(AfxGetInstanceHandle(), szAppPath, kAppPathLen);
	CStringW fixedPath(szAppPath);
	fixedPath = ::MSPath(fixedPath);
	_wsplitpath(fixedPath, szDrive, szDir, szFname, szExt);

	CStringW dllDir(szDrive);
	dllDir += szDir;
	const int dirLen = dllDir.GetLength();
	if (dllDir[dirLen - 1] != L'\\' && dllDir[dirLen - 1] != L'/')
		dllDir += L"\\";

	return dllDir;
}

const CStringW& VaDirs::GetDllDir()
{
	static CStringW sDllDir;
	if (!sDllDir.IsEmpty())
		return sDllDir;

	sDllDir = _GetDllDir();
	return sDllDir;
}

static CString GetCurrentVersion()
{
	// eventually this could record some db version info instead of
	// dll build info so that updates don't require rebuilds of the db
	CString ver;
#ifdef _DEBUG
	CString__FormatA(ver, "%d, %ld",
	                 1000 // prevent automatic rebuild of symbol db in debug builds
	                 ,
	                 g_DBFiles.GetSplitCount());
#else
	CString__FormatA(ver, "%d, %ld", VA_VER_BUILD_NUMBER, g_DBFiles.GetSplitCount());
#endif
	return ver;
}

static bool TestAccess(const CStringW& dir)
{
	// confirm write access to dir
	// this should be done via ACLS check rather than write since Vista VirtualStore might kick in
	TRY
	{
		const CStringW tstFile = dir + L"TestFileAccess";
		CFileW cFile;
		if (!cFile.Open(tstFile,
		                CFile::modeCreate | CFile::modeReadWrite | CFile::shareExclusive | CFile::modeNoInherit))
			return false;
		cFile.Close();
		_wremove(tstFile);
		return true;
	}
	CATCH(CFileException, pEx)
	{
		// failed
		const int kBuflen = 512;
		char msgBuf[kBuflen + 1] = "";
		pEx->GetErrorMessage(msgBuf, kBuflen);
		msgBuf[kBuflen] = '\0';
		OutputDebugString(msgBuf);
	}
	END_CATCH
	return false;
}

#if defined(RAD_STUDIO)

static CStringW GetAndCreateRadStudioVaDir(const CStringW dir)
{
	CStringW retval(dir);
	const WCHAR lastCh = dir[dir.GetLength() - 1];
	if (lastCh != '\\' && lastCh != '/')
		retval += "\\";
	retval += L"VisualAssist";

	// confirm that dir exists
	struct _stat st;
	if (_wstat(retval, &st) || !(st.st_mode & _S_IFDIR))
		CreateDirectoryW(retval, NULL);
	retval += L"\\";

	if (!TestAccess(retval))
		retval.Empty();

	return retval;
}

static CStringW GetAppDataRoamingDir()
{
	_ASSERTE(gVaRadStudioPlugin);
	return GetAndCreateRadStudioVaDir(gVaRadStudioPlugin->GetAppDataRoamingPath());
}

static CStringW GetAppDataLocalDir()
{
	_ASSERTE(gVaRadStudioPlugin);
	return GetAndCreateRadStudioVaDir(gVaRadStudioPlugin->GetAppDataLocalPath());
}

#else

// DO NOT call any functions that will cause logging to occur during GetAndCreateVaSpecialFolder
static CStringW GetAndCreateVaSpecialFolder(int specialFolderId)
{
	CStringW retval;
	WCHAR path[MAX_PATH + 1] = L"";
	if (SHGetSpecialFolderPathW(NULL, path, specialFolderId, TRUE))
	{
		retval = path;
#ifdef AVR_STUDIO
		retval += L"\\VisualAssistAtmel";
#else
		retval += L"\\VisualAssist";
#endif

		// confirm that dir exists
		struct _stat st;
		if (_wstat(retval, &st) || !(st.st_mode & _S_IFDIR))
			CreateDirectoryW(retval, NULL);
		retval += L"\\";
	}
	else
	{
		retval = VaDirs::GetDllDir(); // default to dll dir if SHGetSpecialFolderPath fails
	}

	if (!TestAccess(retval))
		retval.Empty();

	return retval;
}

#if !defined(VA_CPPUNIT)
static CStringW GetAppDataRoamingDir()
{
	return GetAndCreateVaSpecialFolder(CSIDL_APPDATA);
}
#endif

static CStringW GetAppDataLocalDir()
{
	return GetAndCreateVaSpecialFolder(CSIDL_LOCAL_APPDATA);
}

#endif

// return user's roaming dir
const CStringW& VaDirs::GetUserDir()
{
	static CStringW sUserVaDir;
	if (!sUserVaDir.IsEmpty())
		return sUserVaDir;

#if defined(VA_CPPUNIT)
	// [case: 141975]
	// unit tests don't use "UserRoamingDataDir"
	sUserVaDir = sAltDir;
#elif defined(RAD_STUDIO)
	sUserVaDir = GetAppDataRoamingDir();
#else
	// https://docs.wholetomato.com/default.asp?W689
	// #UserRoamingDataDirDuplicated
	const CStringW altRoamingDir = GetRegValueW(HKEY_CURRENT_USER, ID_RK_WT_KEY, "UserRoamingDataDir");
	if (!altRoamingDir.IsEmpty())
	{
		// [case: 96339]
		sUserVaDir = altRoamingDir;
		CreateDir(sUserVaDir);
		if (!TestAccess(sUserVaDir))
		{
			const CStringW tmpDir(GetAppDataRoamingDir());
			if (tmpDir.CompareNoCase(sUserVaDir))
			{
				CStringW msg;
				CString__FormatW(msg,
				                 L"An alternate roaming data directory (%s) has been specified, however Visual Assist "
				                 L"is unable to write to it.\r\n\r\n"
				                 L"Please either fix security/permissions for the directory, or restart after clearing "
				                 L"the UserRoamingDataDir value at \"HKCU\\Software\\Whole Tomato\".",
				                 (LPCWSTR)sUserVaDir);
				MessageBoxW(nullptr, msg, CStringW(IDS_APPNAME), MB_OK | MB_ICONERROR);
			}
		}
	}
	else if (sAltDir.IsEmpty())
	{
		sUserVaDir = GetAppDataRoamingDir();
		// [case: 141975]
		// #UserRoamingDataDirDuplicated
		SetRegValue(HKEY_CURRENT_USER, ID_RK_WT_KEY, "UserRoamingDataDir", sUserVaDir);
	}
	else
	{
		// [case: 141975]
		// for compatibility with old setups, don't change "UserRoamingDataDir" since user
		// may already have state in the alt dir.
		// Due to case 141055, some users may have state in both default location and
		// 'db' dir without realizing it -- we won't do anything about that.
		sUserVaDir = sAltDir;
	}
#endif

	if (sUserVaDir.GetLength())
	{
		if (sUserVaDir[sUserVaDir.GetLength() - 1] != '\\' && sUserVaDir[sUserVaDir.GetLength() - 1] != '/')
			sUserVaDir += "\\";
	}

	_ASSERTE(!sUserVaDir.IsEmpty());
	CreateDir(sUserVaDir + L"Autotext");
	CreateDir(sUserVaDir + L"Autotext\\Saved");
	CreateDir(sUserVaDir + L"Dict");
	CreateDir(sUserVaDir + L"Misc");

	return sUserVaDir;
}

const CStringW& VaDirs::GetUserLocalDir()
{
	static CStringW sUserVaLocalDir;
	if (!sUserVaLocalDir.IsEmpty())
		return sUserVaLocalDir;

	if (sAltDir.IsEmpty())
	{
		_ASSERTE(gShellAttr);
		if (gShellAttr && gShellAttr->IsDevenv10OrHigher())
		{
#ifdef AVR_STUDIO
			// in AVR, VA gets installed in Program Files, not in user
			// extension dir, so Data dir cannot be in install dir.
			// Use same dir as VS2008.
#else
			const int kAppPathLen = MAX_PATH * 2;
			WCHAR szAppPath[kAppPathLen + 1];
			::GetModuleFileNameW(NULL, szAppPath, kAppPathLen);
			CStringW devenvInstallPath = ::Path(szAppPath);
			CStringW vaxDllDir = GetDllDir();

			devenvInstallPath.MakeLower();
			vaxDllDir.MakeLower();

			if (vaxDllDir.Find(devenvInstallPath) >= 0)
			{
				// If the vsix gets installed to all users (/a) into Program Files,
				// then Data dir cannot be in install dir.
				// Use same dir as VS2008.
			}
			else
			{
				// in vs2010, use the extension install dir for the dbs.
				// when the extension is updated or uninstalled, the dbs will
				// be automatically cleared as well.
				sUserVaLocalDir = GetDllDir() + L"Data";

				// confirm that dir exists
				struct _stat st;
				if (_wstat(sUserVaLocalDir, &st) || !(st.st_mode & _S_IFDIR))
					CreateDirectoryW(sUserVaLocalDir, NULL);
				sUserVaLocalDir += L"\\";

				// TestAccess in order to protect against non-local extension installation
				if (!TestAccess(sUserVaLocalDir))
					sUserVaLocalDir.Empty();
			}
#endif
		}

		if (sUserVaLocalDir.IsEmpty())
			sUserVaLocalDir = GetAppDataLocalDir();
	}
	else
		sUserVaLocalDir = sAltDir;

	_ASSERTE(!sUserVaLocalDir.IsEmpty());
	return sUserVaLocalDir;
}

// DO NOT call any functions that will cause logging to occur during UseAlternateDir
void VaDirs::UseAlternateDir(const CStringW& altDir)
{
#if !defined(RAD_STUDIO)
	sAltDir = altDir;
	if (sAltDir.IsEmpty())
		return;
	const int dirLen = sAltDir.GetLength();
	if (sAltDir[dirLen - 1] != L'\\' && sAltDir[dirLen - 1] != L'/')
		sAltDir += L"\\";
	CreateDir(sAltDir);

	// [case: 92000]
	if (!TestAccess(sAltDir))
	{
		CStringW msg;
		CString__FormatW(msg,
		                 L"An alternate data directory (%s) has been specified, however Visual Assist is unable to "
		                 L"write to it.\r\n\r\n"
		                 L"Please either fix security/permissions for the directory, or restart after clearing the "
		                 L"UserDataDir value at \"HKCU\\Software\\Whole Tomato\".",
		                 (LPCWSTR)sAltDir);
		MessageBoxW(NULL, msg, CStringW(IDS_APPNAME), MB_OK | MB_ICONERROR);
	}
#endif
}

bool AcquireGlobalLock(const CStringW& potentialDbDir)
{
#if defined(RAD_STUDIO)
	CStringW lockName(L"VA_RS_");
#else
	CStringW lockName(L"VA_");
#endif
	lockName += potentialDbDir;
	lockName.Replace(L"\\", L"_");
	lockName.Replace(L":", L"_");
	lockName.Replace(L"/", L"_");
	HANDLE hLock = CreateSemaphoreW(NULL, 1, 2, lockName);
	const DWORD err = GetLastError();
	_ASSERTE(!err || ERROR_ALREADY_EXISTS == err);
	if (err)
	{
		if (hLock)
			CloseHandle(hLock);
		return false;
	}

	gProcessLock = hLock;
	return true;
}

const CStringW& VaDirs::GetDbDir()
{
	static CStringW sDbDir;
	if (!sDbDir.IsEmpty())
		return sDbDir;

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		CStringW base(GetUserLocalDir());
		if (base.IsEmpty())
			return sDbDir;

		if (!Psettings)
			Psettings = new CSettings;
		CatLog("Environment.Directories", "PGDB1");
		_ASSERTE(gShellAttr);
		base += gShellAttr->GetDbSubDir();
#ifdef _DEBUG
		base += L"d";
#endif
		CatLog("Environment.Directories", "PGDB2");

		CStringW potentialDbDir;
		// [case: 141791]
		for (int idx = 1; idx < 2000; ++idx)
		{
			CString__FormatW(potentialDbDir, L"%s_%d", (LPCWSTR)base, idx);
			if (AcquireGlobalLock(potentialDbDir))
			{
				sDbDir = potentialDbDir;
				break;
			}
		}

		if (sDbDir.IsEmpty())
			return sDbDir;

		CatLog("Environment.Directories", "PGDB4");
		::CreateDir(sDbDir);
		sDbDir += L"\\";
		::CreateDir(sDbDir + L"cache");
		::CreateDir(sDbDir + L"netImport");
		::CreateDir(sDbDir + L"imports");
		CatLog("Environment.Directories", "PGDB5");
		CatLog("Environment.Directories", (WTString("SetupDbDir: ") + WTString(CString(sDbDir))).c_str());
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("SDD:");
	}
#endif // !SEAN

	_ASSERTE(!sDbDir.IsEmpty());
	return sDbDir;
}

void VaDirs::PurgeDbDir()
{
	const CStringW dbDir(GetDbDir());
	if (dbDir.IsEmpty())
	{
		_ASSERTE(!dbDir.IsEmpty());
		return;
	}

#if !defined(VA_CPPUNIT)
	Log1((WTString("Rebuilding ") + WTString(CString(dbDir))).c_str());
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "lDBDayCount", itos(0).c_str());
#endif // !VA_CPPUNIT

	// Clean old files from previous installs
	CleanDir(dbDir, L"*.va");
	CleanDir(dbDir, L"*.tmp");
	CleanDir(dbDir, L"*.idx");
	CleanDir(dbDir + L"imports/", L"*.cpp");
	CleanDir(dbDir + L"imports/", L"*.tl?");
	CleanDir(dbDir + L"Sys/", L"*.*");

	// New files/dirs
	CleanDir(dbDir + L"cpp/", L"*.*");
	CleanDir(dbDir + L"net/", L"*.*");
	CleanDir(dbDir + L"DbInfo/", L"*.*");
	CleanDir(dbDir, kFileIdDb);
	CleanDbTmpDirs();
	RemoveDbProjectDirs(dbDir);

#if !defined(VA_CPPUNIT)
	const CStringW versionFile(dbDir + kVersionFilename);
	WTofstream ofs(versionFile);
	ofs << GetCurrentVersion();
#endif // !VA_CPPUNIT

	SetStatus(IDS_READY);
}

void VaDirs::PurgeProjectDbs()
{
	if (GlobalProject && GlobalProject->GetFileItemCount())
	{
		::WtMessageBox(
#if defined(RAD_STUDIO)
		               "Purge of project data requires projects to be closed (File | Close All)."
#else
		               "Purge of project data requires solution to be closed (File | Close Solution)."
#endif
		               , IDS_APPNAME, MB_OK | MB_ICONASTERISK);
		return;
	}

	if (g_currentEdCnt || g_EdCntList.size())
	{
		::WtMessageBox(
#if defined(RAD_STUDIO)
		               "Purge of project data requires all editors to be closed (Window | Close All Documents).",
#else
		               "Purge of project data requires all editors to be closed (File | Close All).",
#endif
		               IDS_APPNAME, MB_OK | MB_ICONASTERISK);
		return;
	}

	const CStringW dbDir(GetDbDir());
	CleanDir(dbDir + L"cache", L"*.sdb");
	CleanDir(dbDir + L"cache", L"*.db*");
	CleanDir(dbDir + L"cache", L"*.idx");
	CleanDir(dbDir + L"cache", L"*.tmp");
	CleanDir(dbDir + L"ProjectInfo", L"*.*");
	CleanDir(dbDir + L"DbInfo", L"*.*");
	RemoveDbProjectDirs(dbDir, L"Proj_*");

	::WtMessageBox("Purged all project data.", IDS_APPNAME, MB_OK);
}

void VaDirs::CleanDbTmpDirs()
{
	CleanDir(GetHistoryDir(), L"*.*");

	const CStringW dbDir(GetDbDir());
	if (dbDir.IsEmpty())
	{
		_ASSERTE(dbDir.GetLength());
		return;
	}

	CleanDir(dbDir + L"ProjectInfo", L"*.*");
	CleanDir(dbDir + L"cache", L"*.*");
	CleanDir(dbDir + L"cache/Local/", L"*.*"); // test suite dir
	CleanDir(dbDir + L"netImport", L"*.*");
	CleanDir(dbDir + L"history", L"*.*");
	CleanDir(dbDir + L"typeHist", L"*.*");
	CleanDir(dbDir + L"typeHist/sort", L"*.*");
}

void VaDirs::FlagForDbDirPurge()
{
	// delete the version file
	_wremove(GetDbDir() + kVersionFilename);
}

bool VaDirs::IsFlaggedForDbDirPurge()
{
	// if the version file has been deleted, then the db needs to be purged
	return !IsFile(GetDbDir() + kVersionFilename);
}

void VaDirs::CheckForNewDb()
{
	const bool isNewInstall = IsNewInstallation(); // check before MoveUserDataToUserDir populates the user dir
	MoveUserDataToUserDir();              // call before CheckForAutotextUpdate
	CheckForAutotextUpdate(isNewInstall);          // only check after user dir has been created
	// [case: 132425]
	ConvertAutotextFilesToUtf8();

	if (IsFlaggedForDbDirPurge())
		return; // no version file to check

	const CString curVer(GetCurrentVersion());
	const WTString dbDirVer(ReadFile(GetDbDir() + kVersionFilename));
	if (!dbDirVer.Compare(curVer))
		return; // no change

	// versions differ, so flag for rebuild
	FlagForDbDirPurge();
	// new version file will be created by PurgeDbDir
}

static void RemoveDbDirVariations(const CStringW& dir)
{
	const WCHAR* const kDbDirNames[] = {
#if defined(RAD_STUDIO_LANGUAGE)
		L"CppB",
#else
		L"vs8",
		L"vs8d",
		L"vc8",
		L"vc8d",
		L"vs7",
		L"vs7d",
		L"vc7",
		L"vc7d",
		L"vs70",
		L"vs70d",
		L"vc70",
		L"vc70d",
		L"vc6",
		L"vc6d",
		L"vc5",
		L"evc4",
		L"evc3",
		L"vce",
#endif
		L"testDb",
		L"noIde",
		L""
	};

	CStringW baseDir, curDir;
	for (int idx = 0; *kDbDirNames[idx]; ++idx)
	{
		baseDir = (dir + kDbDirNames[idx] + L"\\");
		RemoveDbDir(baseDir);

		for (int extIdx = 1; extIdx < kMaxDirAttemptsForCleanup; ++extIdx)
		{
			CString__FormatW(curDir, L"%s_%d", (LPCWSTR)baseDir, extIdx);
			RemoveDbDir(curDir);
		}
	}
}

void VaDirs::RemoveAllDbDirs()
{
	const CStringW localDir(GetUserLocalDir());
	RemoveDbDirVariations(localDir);
	RemoveOldDbDirs();
}

void VaDirs::RemoveOldDbDirs()
{
	// old db dirs were in program dir
	const CStringW dllDir(GetDllDir());
	RemoveDbDirVariations(dllDir);

	const CStringW tmpDir(::GetTempDir());
	if (!tmpDir.IsEmpty())
	{
		// old temp dirs didn't use product name
		RemoveDbDirVariations(tmpDir + L"\\");
	}
}

CStringW VaDirs::GetParserSeedFilepath(LPCWSTR basenameAndExt)
{
	CStringW retval(GetUserDir() + L"misc\\" + basenameAndExt);
	if (IsFile(retval))
	{
		vLog("user provided ParserSeedFile: %s", (LPCTSTR)CString(retval));
		return retval;
	}

	retval = GetDllDir() + L"misc\\" + basenameAndExt;
	return retval;
}

const CStringW& VaDirs::GetHistoryDir()
{
	static CStringW histDir;
	if (!histDir.IsEmpty() || !gShellAttr)
		return histDir;

	histDir = GetUserLocalDir();
	histDir += gShellAttr->GetDbSubDir();
#ifdef _DEBUG
	histDir += L"d";
#endif
	if (!IsDir(histDir))
		CreateDir(histDir);
	histDir += L"\\history\\";
	if (!IsDir(histDir))
		CreateDir(histDir);
	return histDir;
}

void VaDirs::LogDirs()
{
	vCatLog("Environment.Directories", "UserDir: %s", (LPCTSTR)CString(GetUserDir()));
	vCatLog("Environment.Directories", "DllDir: %s", (LPCTSTR)CString(GetDllDir()));
	vCatLog("Environment.Directories", "DbDir: %s", (LPCTSTR)CString(GetDbDir()));
}

static void RemoveDbProjectDirs(const CStringW& baseDir, LPCWSTR searchSpec /*= L"Proj*"*/)
{
	FileList projDirs;
	FindFiles(baseDir, searchSpec, projDirs, TRUE);
	for (FileList::const_iterator it = projDirs.begin(); it != projDirs.end(); ++it)
	{
		CStringW curItem((*it).mFilename);
		MyRmDir(curItem + L"/local");
		MyRmDir(curItem + L"/cpp");
		MyRmDir(curItem + L"/net");
		MyRmDir(curItem + L"/temp");
		MyRmDir(curItem + L"/RefDB");
		MyRmDir(curItem);
	}
}

static void RemoveDbDir(const CStringW& dbDir)
{
	if (!IsDir(dbDir))
		return;

	MyRmDir(dbDir + L"imports");
	MyRmDir(dbDir + L"history");
	MyRmDir(dbDir + L"cache\\local");
	MyRmDir(dbDir + L"cache");
	MyRmDir(dbDir + L"netImport");
	MyRmDir(dbDir + L"cpp");
	MyRmDir(dbDir + L"Sys");
	MyRmDir(dbDir + L"net");
	MyRmDir(dbDir + L"System");
	MyRmDir(dbDir + L"typeHist\\sort");
	MyRmDir(dbDir + L"typeHist");
	RemoveDbProjectDirs(dbDir);
	MyRmDir(dbDir);
}

static bool IsNewInstallation()
{
	const CStringW userFile = VaDirs::GetUserDir() + L"Autotext/cpp.tpl";
	if (IsFile(userFile))
		return false;

	FileList userAutotextFiles;
	FindFiles(VaDirs::GetUserDir() + L"Autotext", L"*.tpl", userAutotextFiles);
	if (userAutotextFiles.size())
		return false;

	const CStringW oldUserFile = VaDirs::GetDllDir() + L"Autotext/cpp.tpl";
	if (IsFile(oldUserFile))
		return false;

	FindFiles(VaDirs::GetDllDir() + L"Autotext", L"*.tpl", userAutotextFiles);
	if (userAutotextFiles.size())
		return false;

	return true;
}

static void CopyDefaultAutotextFileToUserDir(const CStringW& basename)
{
	const CStringW targetFile(VaDirs::GetUserDir() + L"Autotext/" + basename + L".tpl");
	if (IsFile(targetFile))
		return;

	CStringW sourceFile = VaDirs::GetDllDir() + L"Autotext/" + basename + L".tpl";
	if (IsFile(sourceFile))
		Copy(sourceFile, targetFile);
	else
	{
		sourceFile = VaDirs::GetDllDir() + L"Autotext/latest/" + basename + L".tpl";
		if (IsFile(sourceFile))
		{
			Copy(sourceFile, targetFile);
			_wchmod(targetFile, _S_IREAD | _S_IWRITE);
		}
	}
}

// migrates from old user dir to new user dir
static void MoveUserDataToUserDir()
{
	CStringW targetFile = VaDirs::GetUserDir() + L"Autotext/cpp.tpl";
	if (!IsFile(targetFile))
	{
		const CStringW kBaseAutotextNames[] = {L"cpp", L"html", L"vb", L"perl", L"js", L"cs", L"java", L"txt", L""};

		for (int idx = 0; kBaseAutotextNames[idx].GetLength(); ++idx)
			CopyDefaultAutotextFileToUserDir(kBaseAutotextNames[idx]);
	}

#if !defined(RAD_STUDIO) // RAD_STUDIO doesn't need to deal with old directory structure
	// spell checker user words dictionary file
	const CStringW kDictUserWords(VaDirs::GetUserDir() + L"Dict\\UserWords.txt");
	if (IsFile(kDictUserWords))
		return; // assume migrated if userwords file exists (skip rest of files)
	else
	{
		const CStringW kOldAppUserwords(VaDirs::GetDllDir() + L"Dict\\UserWords.txt");
		if (IsFile(kOldAppUserwords))
			Copy(kOldAppUserwords, kDictUserWords);
		else
		{
			// build 1260-1267
			const CStringW kOldAppUserwords1260(VaDirs::GetDllDir() + L"Dic\\UserWords.txt");
			if (IsFile(kOldAppUserwords1260))
				Copy(kOldAppUserwords1260, kDictUserWords);
		}
	}

	// vatree files
	CStringW sourceFile;
	targetFile = VaDirs::GetUserDir() + L"misc/VAssist.fil";
	if (!IsFile(targetFile))
	{
		sourceFile = VaDirs::GetDllDir() + L"misc/Vassist.fil";
		if (IsFile(sourceFile))
			Copy(sourceFile, targetFile);

		targetFile = VaDirs::GetUserDir() + L"misc/VAssist.mth";
		if (!IsFile(targetFile))
		{
			sourceFile = VaDirs::GetDllDir() + L"misc/Vassist.mth";
			if (IsFile(sourceFile))
				Copy(sourceFile, targetFile);
		}

		targetFile = VaDirs::GetUserDir() + L"misc/VAssist.cpy";
		if (!IsFile(targetFile))
		{
			sourceFile = VaDirs::GetDllDir() + L"misc/Vassist.cpy";
			if (IsFile(sourceFile))
				Copy(sourceFile, targetFile);
		}

		targetFile = VaDirs::GetUserDir() + L"misc/VAssist.bmk";
		if (!IsFile(targetFile))
		{
			sourceFile = VaDirs::GetDllDir() + L"misc/Vassist.bmk";
			if (IsFile(sourceFile))
				Copy(sourceFile, targetFile);
		}
	}
#endif
}

void BackupPreUtf8AutotextFile(const CStringW& theFile)
{
	_ASSERTE(IsFile(theFile));

	CStringW backupDir(Path(theFile));
	backupDir += L"\\Saved.PreUtf8";
	if (!IsDir(backupDir))
		CreateDir(backupDir);

	CStringW bakFile(backupDir + L"\\" + Basename(theFile));
	Copy(theFile, bakFile, true);
}

void ConvertAutotextFileToUtf8(const CStringW& theFile)
{
	WTString fileSource;
	if (!fileSource.ReadFile(theFile))
		return;

	CFileW file;
	if (!file.Open(theFile, CFile::modeCreate | CFile::modeWrite /*, &e*/))
	{
		_ASSERTE(!"Save file open error");
		return;
	}

	file.WriteUtf8Bom();
	if (fileSource[0] != '\f')
	{
		// write out empty initial snippet for backwards compatibility with
		// versions of VA that don't know the file is utf8 (ascii text will be fine,
		// but non-ascii text will not work in those versions).
		char fileHeader[2] = {'\f', '\n'};
		file.Write(fileHeader, 2);
	}

	file.Write(fileSource.c_str(), (UINT)fileSource.GetLength());
	file.Close();
}

void ConvertAutotextFilesToUtf8()
{
	// [case: 132425]
	FileList userAutotextFiles;
	FindFiles(VaDirs::GetUserDir() + L"Autotext", L"*.tpl", userAutotextFiles);
	for (const auto& it : userAutotextFiles)
	{
		if (!GetFSize(it.mFilename))
			continue;

		if (HasUtf8Bom(it.mFilename))
			continue;

		BackupPreUtf8AutotextFile(it.mFilename);
		ConvertAutotextFileToUtf8(it.mFilename);
	}
}

static void CheckForAutotextUpdate(bool isNewInstall)
{
	// can't do this at install since it needs to be done for each user
	// Note: call MoveUserDataToUserDir before SetupAutotextDir is called

	const CStringW kUserAutotextVerFile(VaDirs::GetUserDir() + L"Autotext\\" + kVersionFilename);
	const WTString userAutotextVer(ReadFile(kUserAutotextVerFile));

	const CStringW kLatestAutotextVerFile(VaDirs::GetDllDir() + L"Autotext\\Latest\\" + kVersionFilename);
	WTString latestAutotextVer(ReadFile(kLatestAutotextVerFile));
	if (latestAutotextVer.IsEmpty())
		latestAutotextVer = "2008.11.04";

	if (!userAutotextVer.IsEmpty() && userAutotextVer.Compare(latestAutotextVer) >= 0)
	{
		if (IsFile(VaDirs::GetUserDir() + L"Autotext\\cpp.tpl"))
			return; // no change

		FileList userAutotextFiles;
		FindFiles(VaDirs::GetUserDir() + L"Autotext", L"*.tpl", userAutotextFiles);
		if (userAutotextFiles.size())
			return; // no change
	}

	_wremove(kUserAutotextVerFile);

	SetupAutotextDir(!isNewInstall);
	AutotextManager::InstallDefaultItems();

	WTofstream ofs(kUserAutotextVerFile);
	ofs << latestAutotextVer.c_str();
}

static void SetupAutotextDir(bool promptUser)
{
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		const WTString promptMsg("Do you want to install the latest versions of VA Snippets? If yes, your current "
		                         "versions will be moved to Autotext\\Saved in the Visual Assist installation "
		                         "directory. If no, you can review the latest versions in Autotext\\Latest.");
		const CStringW kInstallDir(VaDirs::GetDllDir());
		FileList latestAutotext;
		FindFiles(kInstallDir + L"Autotext\\Latest", L"*.tpl", latestAutotext);
		bool doUpdate = true;

		for (FileList::const_iterator it = latestAutotext.begin(); it != latestAutotext.end(); ++it)
		{
			const CStringW newFile((*it).mFilename);
			const CStringW kUserDir(VaDirs::GetUserDir());
			const CStringW userFile(kUserDir + L"Autotext\\" + Basename(newFile));
			const CStringW savedFile(kUserDir + L"Autotext\\Saved\\" + Basename(newFile));

			if (IsFile(userFile))
			{
				if (!FileTimesAreEqual(newFile, userFile))
				{
					if (promptUser)
					{
						promptUser = false;
						doUpdate = WtMessageBox(promptMsg, IDS_APPNAME, MB_YESNO) == IDYES;
					}

					if (doUpdate)
					{
						_wchmod(savedFile, _S_IREAD | _S_IWRITE);
						_wremove(savedFile);
						Copy(userFile, savedFile, true);

						_wchmod(userFile, _S_IREAD | _S_IWRITE);
						_wremove(userFile);
						Copy(newFile, userFile, true);
					}
				}
			}
			else
			{
				// Templates directory is from old builds prior to user data dir split (pre-1545)
				// Templates dir only exists in dll dir
				const CStringW oldFile(kInstallDir + L"Templates\\" + Basename(newFile));
				if (IsFile(oldFile))
				{
					const CStringW oldLatestFile(kInstallDir + L"Templates\\Latest\\" + Basename(newFile));
					if (IsFile(oldLatestFile) && FileTimesAreEqual(oldFile, oldLatestFile))
					{
						Copy(newFile, userFile, true);
					}
					else
					{
						if (promptUser)
						{
							promptUser = false;
							doUpdate = WtMessageBox(promptMsg, IDS_APPNAME, MB_YESNO) == IDYES;
						}

						if (doUpdate)
						{
							Copy(newFile, userFile, true);

							_wchmod(savedFile, _S_IREAD | _S_IWRITE);
							_wremove(savedFile);
							Copy(oldFile, savedFile, true);
						}
						else
						{
							Copy(oldFile, userFile, true);
						}
					}
				}
				else
				{
					Copy(newFile, userFile, true);
				}
			}
			_wchmod(userFile, _S_IREAD | _S_IWRITE);
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("SAD:");
	}
#endif // !SEAN
}
