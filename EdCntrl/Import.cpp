#include "stdafxed.h"
#include "mparse.h"
#include "log.h"
#include "IncToken.h"
#include "Timer.h"
#include "macrolist.h"
#include "DevShellAttributes.h"
#include "project.h"
#include "file.h"
#include "FileTypes.h"
#include "wt_stdlib.h"
#include "StatusWnd.h"
#include "assert_once.h"
#include "resource.h"
#include "Settings.h"
#include "Lock.h"
#include "FileId.h"
#include "Directories.h"
#include "TokenW.h"
#include "StringUtils.h"
#include "Import.h"
#include "DTypeDbScope.h"
#include "SpinCriticalSection.h"
#include "inheritanceDb.h"
#include "DllNames.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MSC_VER > 1200
using std::ios;
using std::ofstream;
#endif

static CStringW GetCompilerPath();
static CStringW GetVCVarsPath();
static void SetupPCHFile();
static void RunCompile(const CStringW& importDir, LPCWSTR fileToCompile, const CStringW& binaryFile,
                       LPCWSTR outputFile);

extern long g_haltImportCnt;

CStringW Import(const CStringW& file)
{
#if defined(RAD_STUDIO)
	_ASSERTE(!"why is Import being called?");
	return CStringW();
#else
	DEFTIMERNOTE(ImportTimer, CString(file));
	LOG2("Import");
	static const CStringW importDir = VaDirs::GetDbDir() + L"imports\\";
	CStringW importNameIn(file);
	importNameIn.MakeLower();
	const CStringW bname = Basename(importNameIn);
	int pos = bname.ReverseFind(L'.');
	const CStringW sname = bname.Left(pos != -1 ? pos : bname.GetLength());
	const CStringW tlh = importDir + sname + L".tlh";

	if (!Psettings->m_parseImports)
	{
		if (!IsFile(tlh))
		{
			WTofstream ofs(tlh);
			ofs << "// import processing disabled"
			    << "\r\n";
			ofs.close();
			vLog("import processing disabled (%s)", (const char*)CString(tlh));
			SetFTime(tlh, file);
		}
		return tlh;
	}

	// name of cpp file that we generate
	const CStringW tcppfile = importDir + sname + L".cpp";
	// get #import args as they appear in the source file being processed by MParse::FormatFile
	_ASSERTE(!"Import line needs fixing"); // line below needs fixin
	const WTString args = NULLSTR;         // g_pGlobDic->Find(bname + "_import_args", type);
	WTString generatedCppCode;
	// create the contents of the generated cpp file
	generatedCppCode.WTFormat("#include \"VAImportPch.h\"\n#import \"%S\" %s\n#error stop\n",
	                          (const wchar_t*)importNameIn, args.c_str());

	if (IsFile(tlh))
	{
		// check file times - return tlh w/o further processing if ok
		if (FileTimesAreEqual(tlh, file))
		{
			// verify contents of cpp file on disk against the current set of args
			WTString readCode;
			readCode.ReadFile(tcppfile);

			if (!readCode.Compare(generatedCppCode))
				return tlh; // filetime match and import arg match
			else if (g_haltImportCnt)
			{
				// filetime match but import args don't match
				//   but user is still typing
				vLog("skipping import (%S) (out of sync - while typing): was (%s)\nshould be: (%s)",
				     (const wchar_t*)file, readCode.c_str(), generatedCppCode.c_str());
				return tlh;
			}
			// recreate tlh
			vLog("importing (%S) (out of sync): was (%s)\nshould be: (%s)", (const wchar_t*)file, readCode.c_str(),
			     generatedCppCode.c_str());
		}
		else
		{
			HANDLE fp;
			FILETIME creation, lastAccess, lastWrite;
			vLog("importing (%s): filetime mismatch", (const char*)CString(file));
			fp = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			                 OPEN_EXISTING, FILE_ATTRIBUTE_ARCHIVE, NULL);
			if (INVALID_HANDLE_VALUE != fp)
			{
				WTGetFileTime(fp, &creation, &lastAccess, &lastWrite);
				CloseHandle(fp);
				vLog("\ttimes for (%s)\n\t%08lx.%08lx, %08lx.%08lx, %08lx.%08lx\n", (const char*)CString(file),
				     creation.dwHighDateTime, creation.dwLowDateTime, lastAccess.dwHighDateTime,
				     lastAccess.dwLowDateTime, lastWrite.dwHighDateTime, lastWrite.dwLowDateTime);
			}
			else
			{
				vLog("ERROR: Import unable to create file %s", (const char*)CString(file));
			}

			fp = CreateFileW(tlh, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			                 FILE_ATTRIBUTE_ARCHIVE, NULL);
			if (INVALID_HANDLE_VALUE != fp)
			{
				WTGetFileTime(fp, &creation, &lastAccess, &lastWrite);
				CloseHandle(fp);
				vLog("\ttimes for (%s)\n\t%08lx.%08lx, %08lx.%08lx, %08lx.%08lx\n", (const char*)CString(tlh),
				     creation.dwHighDateTime, creation.dwLowDateTime, lastAccess.dwHighDateTime,
				     lastAccess.dwLowDateTime, lastWrite.dwHighDateTime, lastWrite.dwLowDateTime);
			}
			else
			{
				vLog("ERROR: Import unable to create file %s", (const char*)CString(tlh));
			}
		}
	}
	else
	{
		vLog("importing (%s): missing (%s)", (const char*)CString(file), (const char*)CString(tlh));
	}

	// cleanup any residual files
	{
		// need to delete all cached info about the tlh and tli (d1, rtf)
		//   since time of new tlh won't be different than the old one
		//   if the only thing that changed are #import args

		// TODO: change the filetime we have recorded so that MarkIncluded
		//   doesn't think we're up to date
		//   Right now, this is dealt with in a MultiParse::ProcessIncludeLn hack

		CStringW tmpFile2;

		tmpFile2 = tlh;
		tmpFile2 = MSPath(tmpFile2);
		_wremove(tlh);
		MultiParsePtr mp = MultiParse::Create(Src);
		mp->RemoveAllDefs(tmpFile2, DTypeDbScope::dbSlnAndSys);

		tmpFile2 = importDir + sname + L".tli";
		tmpFile2 = MSPath(tmpFile2);
		_wremove(tmpFile2);
		mp->RemoveAllDefs(tmpFile2, DTypeDbScope::dbSlnAndSys);
	}

	// create cpp file that will be passed to cl
	{
		WTofstream ofs(tcppfile, ios::binary);
		ofs << generatedCppCode.c_str();
		ofs.close();
	}

	// make sure precompiled header is ready
	SetupPCHFile();

	// pass to cl.exe
	RunCompile(importDir, tcppfile, file, tlh);

	// set ftime to match binary - compiler does it, but may be problems due to NTFS/FAT
	SetFTime(tlh, file);
	if (!FileTimesAreEqual(tlh, file))
	{
		// NTFS -> FAT problem?
		static int cnt = 1;
		if (cnt && ++cnt > 6)
		{
			cnt = 0;
			WTString msg("Errors have occurred processing #import directives.\nFurther processing of #imports will be "
			             "disabled.\nPlease contact us at http://www.wholetomato.com/contact \nThis message will only "
			             "be displayed once per session.");
			ErrorBox(msg);
			Psettings->m_parseImports = false;
		}
	}

	return tlh;
#endif
}

#if !defined(RAD_STUDIO)
CStringW GetCompilerPath()
{
	static CStringW clPath;
	if (clPath.GetLength() && IsFile(clPath))
		return clPath;

	IncludeDirs impTok;
	TokenW itok = impTok.getImportDirs();

	while (itok.more())
	{
		CStringW path = itok.read(L";");
		if (!path.GetLength())
			path = L".";
		path += L"\\";
		path += L"cl.exe";
		if (IsFile(path))
		{
			if (path.Find(L' ') != -1)
				clPath = L"\"";
			clPath += path;
			if (path.Find(L' ') != -1)
				clPath += L"\"";
			return clPath;
		}
	}

	// couldn't find it - need to return something...
	Log("ERROR cl.exe is not found in the system path");
	clPath = L"cl.exe";
	return clPath;
}

CStringW GetVCVarsPath()
{
	static CStringW vcVarsPath;
	if (vcVarsPath.GetLength() && IsFile(vcVarsPath))
		return vcVarsPath;

	IncludeDirs impTok;
	TokenW itok = impTok.getImportDirs();

	while (itok.more())
	{
		CStringW path = itok.read(L";");
		if (!path.GetLength())
			path = L".";
		path += L"\\";
		path += L"vcvars32.bat";
		if (IsFile(path))
		{
			if (path.Find(L' ') != -1)
				vcVarsPath = L"\"";
			vcVarsPath += path;
			if (path.Find(L' ') != -1)
				vcVarsPath += L"\"";
			return vcVarsPath;
		}
	}

	// couldn't find it - need to return something...
	Log("ERROR vcvars32.bat is not found in the system path");
	return vcVarsPath;
}

void SetupPCHFile()
{
	static const CStringW pchCompiledFile = VaDirs::GetDbDir() + L"imports\\VAImportPch.pch";
	static const CStringW pchHeaderFile = VaDirs::GetDbDir() + L"imports\\VAImportPch.h";
	static const CStringW pchCppFile = VaDirs::GetDbDir() + L"imports\\VAImportPch.cpp";

	if (IsFile(pchCompiledFile) && IsFile(pchHeaderFile) && IsFile(pchCppFile))
		return;

	// the import.pif file will be installed to the misc dir - copy it
	//   into the current env imports dir
	if (!IsFile(VaDirs::GetDbDir() + L"imports\\import.pif"))
		CopyFileW(VaDirs::GetDllDir() + L"misc\\import.pif", VaDirs::GetDbDir() + L"imports\\import.pif", FALSE);

	if (!IsFile(pchHeaderFile))
	{
		WTofstream ofs(pchHeaderFile, ios::binary);
		ofs << "#pragma once\n#include <comdef.h>\n";
		ofs.close();
	}
	if (!IsFile(pchCppFile))
	{
		WTofstream ofs(pchCppFile, ios::binary);
		ofs << "#include \"VAImportPch.h\"\n";
		ofs.close();
	}

	// compile the cpp file to generate the pch
	RunCompile(VaDirs::GetDbDir() + L"imports\\", pchCppFile, pchCompiledFile, NULL);

	if (!IsFile(pchCompiledFile))
	{
		Log("ERROR creating import pch file");
		ASSERT(FALSE);
	}
}

void RunCompile(const CStringW& importDir, LPCWSTR fileToCompile, const CStringW& binaryFile, LPCWSTR outputFile)
{
	WTString errMsg;
	DWORD lastErr = 0;

	// build cl command w/ args
	CStringW cmd;
	CString__FormatW(cmd, L"%s /Zs /FpVAImportPch /YXVAImportPch /DNDEBUG \"%s\"", (const wchar_t*)GetCompilerPath(),
	                 fileToCompile);
	const std::unique_ptr<WCHAR[]> cmdLineStore(new WCHAR[1024]);
	WCHAR* cmdLine = &cmdLineStore[0];

	// generate batch file
	const CStringW vcvarsbat = GetVCVarsPath();
	if (vcvarsbat.GetLength())
	{
		CStringW batFileName(importDir);
		batFileName += L"import.bat";
		HANDLE hBatFile = CreateFileW(batFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hBatFile != INVALID_HANDLE_VALUE)
		{
			DWORD bytesWritten;
			CStringW batCmds;
			CString__FormatW(batCmds, L"call %s\r\n%s\r\n", (const wchar_t*)vcvarsbat, (const wchar_t*)cmd);
			if (WriteFile(hBatFile, batCmds, batCmds.GetLength() * sizeof(WCHAR), &bytesWritten, NULL))
			{
				_ASSERTE((int)bytesWritten == batCmds.GetLength() * sizeof(WCHAR));
				if (batFileName.Find(L' ') != -1)
				{
					batFileName = L"\"" + batFileName + L"\"";
				}
				wcscpy(cmdLine, batFileName);
			}
			else
			{
				lastErr = GetLastError();
				vLog("ERROR write to import.bat failed %08lx", lastErr);
				static bool once = true;
				if (once)
				{
					once = false;
					errMsg.WTFormat("An error occurred preparing a #import directive (WriteFile error %08lx).",
					                lastErr);
					ErrorBox(errMsg, MB_OK | MB_ICONERROR);
				}
			}
			FlushFileBuffers(hBatFile);
			CloseHandle(hBatFile);
		}
		else
		{
			lastErr = GetLastError();
			DeleteFileW(batFileName);
			vLog("ERROR failed to create (%s) %08lx", (const char*)CString(batFileName), lastErr);
			static bool once = true;
			if (once)
			{
				once = false;
				errMsg.WTFormat("An error occurred preparing a #import directive (CreateFile error %08lx for %S).",
				                lastErr, (const wchar_t*)batFileName);
				ErrorBox(errMsg, MB_OK | MB_ICONERROR);
			}
		}
		Sleep(50); // give chance for file to flush
	}
	// if no batch file, then use raw cl.exe command
	if (cmdLine[0] == L'\0')
		wcscpy(cmdLine, cmd);

	WTString msg("Creating .tlh and .tli files for ");
	msg += CString(binaryFile);
	SetStatus(msg);
	vLog("\t%s\n", (const char*)CString(cmdLine));
	STARTUPINFOW sinfo;
	PROCESS_INFORMATION pinfo;
	ZeroMemory(&sinfo, sizeof(STARTUPINFOW));
	ZeroMemory(&pinfo, sizeof(PROCESS_INFORMATION));
	sinfo.cb = sizeof(STARTUPINFOW);
	sinfo.dwFlags = STARTF_USESHOWWINDOW;
	sinfo.wShowWindow = SW_HIDE;
	if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP | HIGH_PRIORITY_CLASS, NULL,
	                   importDir, &sinfo, &pinfo))
	{
		static bool once = true;
		if (!outputFile)
			VAProcessMessages(); // building pch may take a couple seconds
		DWORD dwResult = WaitForSingleObject(pinfo.hProcess, Psettings->m_ImportTimeout * 1000);
		if (dwResult == WAIT_TIMEOUT)
		{
			vLog("ERROR wait createprocess timeout %ld", Psettings->m_ImportTimeout * 1000);
			if (!outputFile)
			{
				Log("ERROR wait timeout creating import pch file");
				ASSERT(FALSE);
			}
			else if (once)
			{
				once = false;
				ErrorBox("Timed out waiting for cl.exe to finish creating .tlh and .tli files.\nContact us at "
				         "http://www.wholetomato.com/contact if you would like to increase the timeout value.",
				         MB_OK | MB_ICONASTERISK);
			}
			else
			{
				Log("ERROR multiple import timeout");
				ASSERT(FALSE);
			}
		}
		CloseHandle(pinfo.hThread);
		CloseHandle(pinfo.hProcess);
	}
	else
	{
		lastErr = GetLastError();
		vLog("ERROR createprocess failed (%s) (%s) (%08lx)", (const char*)CString(cmdLine), (const char*)CString(cmd),
		     lastErr);
		static bool once = true;
		if (once)
		{
			once = false;
			errMsg.WTFormat("An error occurred preparing a #import directive (CreateProcess error %08lx).", lastErr);
			ErrorBox(errMsg, MB_OK | MB_ICONERROR);
		}
	}

	Sleep(50); // give chance for file to flush
	if (outputFile && !IsFile(outputFile))
	{
		// file didnt create, touch so we have something to parse
		WTofstream ofs(outputFile);
		ofs << "// error generating file with cmd: " << CString(cmdLine) << "\r\n";
		ofs.close();
		vLog("ERROR generating file %s from cmd %s (in %S)", (const char*)CString(outputFile),
		     (const char*)CString(cmdLine), (const wchar_t*)cmd);
		_ASSERTE(!"import compile error");
		/*		// let user send us mail if there's a coloring problem rather than displaying this msg
		        static once = false;
		        if (once)
		        {
		            once = false;
		            WTString msg;
		            msg.Format("An error occurred processing a #import directive.\nPlease contact us at
		   http://www.wholetomato.com/contact and send the contents of the following directory:\n\"%s\"\nAlso please
		   copy and send the contents of the About page in the Visual Assist Options dialog.\nThis message will only be
		   displayed once per session.", importDir); ErrorBox(msg);
		        }
		*/
	}
}
#endif

class NetReferenceImporter
{
  public:
	NetReferenceImporter() : mActive(false)
	{
	}
	~NetReferenceImporter()
	{
		if (mActive)
			RemoveFromActiveList();
	}

	bool Import(const CStringW& path, MultiParse& mp)
	{
		_ASSERTE(path && *path);

		bool rslt = false;
		Init(path);
		if (AddToActiveList())
		{
			if (DoNetImport(mp))
			{
				CatLog("Parser.Import", "NetReferenceImporter::Import: Success.");
				rslt = true;
			}
			else
			{
				CatLog("Parser.Import", "NetReferenceImporter::Import: Fail");
			}
		}
		return rslt;
	}

  private:
	CStringW mPath;
	CStringW mPathKey;
	bool mActive;
	static std::set<CStringW> mActiveImports;
	static CSpinCriticalSection mActiveImportsLock;

	void Init(const CStringW& path)
	{
		mPath = path;
		mPath.MakeLower();
		mPathKey = mPath;
		mPathKey.Replace(L'.', L'|');
		mPathKey.Replace(L':', L'|');
		mPathKey = CStringW(L":VAIMPORT_PATH:") + mPathKey;
		// any change to pathKey needs to be mirrored in VANetObjMD/ExportDb.cpp.
	}

	bool AddToActiveList()
	{
		if (!mPath.GetLength())
			return false;

		AutoLockCs l(mActiveImportsLock);
		if (mActiveImports.find(mPath) != mActiveImports.cend())
			return false;

		mActiveImports.insert(mPath);
		mActive = true;
		return true;
	}

	void RemoveFromActiveList()
	{
		_ASSERTE(mActive);
		AutoLockCs l(mActiveImportsLock);
		mActiveImports.erase(mPath);
	}

	bool DoNetImport(MultiParse& mp)
	{
		const CStringW dfile = ::BuildNetImportPath(mPath);

		vCatLog("Parser.Import", "NetReferenceImporter::DoNetImport: Search for: %s\n  dfile = %s\n", (const char*)CString(mPath),
		     (const char*)CString(dfile));
		if (::IsFile(dfile))
		{
			if (::FileTimesAreEqual(dfile, mPath))
			{
				if (!mp.FindExact2(WTString(mPathKey), true, 0, false))
				{
					// this is unusual... dfile was created but hadn't been
					// converted into dbfile
					CatLog("Parser.Import", "NetReferenceImporter::DoNetImport: -- dfile found -- not in dbfile?");
					::GetSysDic(CS)->m_modified++;
					mp.ReadDFile(dfile, V_SYSLIB, VA_DB_Net, (char)0x01);
				}
				else
				{
					// this will be the normal case if the dfile already exists
					// since dfiles are loaded into dbfile when created
					CatLog("Parser.Import", "NetReferenceImporter::DoNetImport: found in dictionary by path");
				}
				return true;
			}
			else
			{
				if (mp.FindExact2(WTString(mPathKey), true, 0, false))
				{
					CatLog("Parser.Import", "NetReferenceImporter::DoNetImport: -- stale dfile found -- already loaded into db");
					// remove dfile defs from dbfile
					// [case: 94805] mp.RemoveAllDefs can cause busy hang; leaving potential duplicates (previous
					// behavior for years...) mp.RemoveAllDefs(dfile, DTypeDbScope::dbSystem); at least prune the
					// inheritance db -- safe since it doesn't use the hasthable db lock
					InheritanceDb::PurgeInheritance(dfile, DTypeDbScope::dbSystem);
				}
				else
				{
					CatLog("Parser.Import", "NetReferenceImporter::DoNetImport: -- stale unloaded dfile found");
				}
			}
		}
		else
		{
			CatLog("Parser.Import", "NetReferenceImporter::DoNetImport: -- dfile not found");
		}

		const std::unique_ptr<WCHAR[]> cmdLineStore(new WCHAR[2045]);
		WCHAR* cmdLine = &cmdLineStore[0];
		::swprintf_s(cmdLine, 2045, L"\"%s" IDS_VANETOBJMD_DLLW "\" \"%s\" \"%s\"", (LPCWSTR)VaDirs::GetDllDir(),
		             (LPCWSTR)mPath, (LPCWSTR)dfile);

		WTString msg;
		msg.WTFormat("Importing %s...", WTString(mPath).c_str());
		SetStatus(msg);
		vCatLog("Parser.Import", "\t%s\n", (const char*)CString(cmdLine));
		STARTUPINFOW sinfo;
		PROCESS_INFORMATION pinfo;
		ZeroMemory(&sinfo, sizeof(STARTUPINFOW));
		ZeroMemory(&pinfo, sizeof(PROCESS_INFORMATION));
		sinfo.cb = sizeof(STARTUPINFOW);
		sinfo.dwFlags = STARTF_USESHOWWINDOW;
		sinfo.wShowWindow = SW_HIDE;
		GetSysDic(CS)->m_modified++;
		if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP | HIGH_PRIORITY_CLASS, NULL, NULL,
		                   &sinfo, &pinfo))
		{
			DWORD dwResult = WaitForSingleObject(pinfo.hProcess, Psettings->m_ImportTimeout * 1000);
			if (dwResult == WAIT_TIMEOUT)
			{
				vLog("ERROR wait createprocess timeout %ld", Psettings->m_ImportTimeout * 1000);
				ASSERT_ONCE(FALSE);
			}
			else if (dwResult != 0)
			{
				vLog("ERROR wait createprocess error %ld", dwResult);
				ASSERT_ONCE(FALSE);
			}
			BOOL bThreadClosed = CloseHandle(pinfo.hThread);
			BOOL bProcessClosed = CloseHandle(pinfo.hProcess);
			if (!bThreadClosed || !bProcessClosed)
			{
				vLog("ERROR createprocess close handle (%d,%d)", bThreadClosed, bProcessClosed);
				ASSERT_ONCE(FALSE);
			}
		}
		else
		{
			DWORD lastErr = GetLastError();
			vLog("ERROR createprocess failed (%s) (%s) (%08lx)", (const char*)CString(cmdLine),
			     (const char*)CString(mPath), lastErr);
#if !defined(VA_CPPUNIT)
			ASSERT_ONCE(FALSE);
#endif
		}

		Sleep(50); // give chance for file to flush
		if (IsFile(dfile))
		{
			::SetFTime(dfile, mPath);
			mp.ReadDFile(dfile, V_SYSLIB, VA_DB_Net, (char)0x01);
			return true;
		}
		else
		{
			// 			const char * valName = "NetImportInstallChecked";
			// 			static bool sPrompted = GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, valName, false);
			// 			if (!sPrompted && g_pCSDic && g_pCSDic->m_loaded)
			// 			{
			// 				// [case: 49731] check for installation of .net 2 or 3, prompt once if not installed
			// 				const CStringW frameworkDirs(::GetFrameworkDirs());
			// 				if (-1 == frameworkDirs.Find(L"v2.") && -1 == frameworkDirs.Find(L"v3."))
			// 				{
			// 					const int res = ::MessageBox(gMainWnd->GetSafeHwnd(),
			// 						"An operation failed possibly due to an incompatible version of the .NET
			// Framework.\r\n\r\n" 						"Please contact Whole Tomato support at
			// http://www.wholetomato.com/contact
			// to diagnose the problem.\r\n\r\n" 						"This message will not be displayed again.",
			// IDS_APPNAME, MB_OK | MB_ICONERROR);
			// 				}
			//
			// 				sPrompted = true;
			// 				SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, valName, sPrompted);
			// 			}

			LogUnfiltered((const char*)(CString("NetReferenceImporter::DoNetImport: Error: dfile not created: ") +
			                  CString(dfile)));
			return false;
		}
	}
};

std::set<CStringW> NetReferenceImporter::mActiveImports;
CSpinCriticalSection NetReferenceImporter::mActiveImportsLock;

void NetImportDll(const CStringW dll)
{
#if !defined(VAX_CODEGRAPH) && !defined(RAD_STUDIO)
	CatLog("Parser.Import", (const char*)(CString("NetImportDll: ") + CString(dll)));

	if (dll.IsEmpty())
	{
		Log("  bad param");
		return;
	}

	if (!IsFile(dll))
	{
		Log("  not a file");

#ifdef _DEBUG
		if (!IsDir(dll))
		{
			const CStringW ext(GetBaseNameExt(dll));
			if (ext.CompareNoCase(L"dll") && ext.CompareNoCase(L"exe") && ext.CompareNoCase(L"winmd"))
			{
				// prevent assert for project dependency where dll hasn't been built yet
				_ASSERTE(!"what are you trying to feed to NetImportDll (not a file)?");
			}
		}
#endif
		return;
	}

	const int fType = GetFileType(dll);
	if (fType != Binary && fType != Other)
	{
		Log("  bad file type");
		_ASSERTE(!"what kind of file are you trying to feed to NetImportDll?");
		return;
	}

	GetSysDic(CS);

	MultiParsePtr mp = MultiParse::Create(CS);

	NetReferenceImporter imp;
	imp.Import(dll, *mp);
	SetStatus(IDS_READY);
#endif
}

// This is used during import of NET symbols - VaNetObj outputs old format
CStringW BuildNetImportPath(const CStringW& file)
{
	// include drive in hash string - to fix Walter's problem
	return MSPath(CStringW(VaDirs::GetDbDir() + L"netImport/" + (file[0] ? file[0] : L'x') +
	                       itosw((int)gFileIdManager->GetFileId(file)) + L".d1"));
}
