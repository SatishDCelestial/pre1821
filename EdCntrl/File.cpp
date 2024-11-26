#include "stdafxed.h"
#define FILEFINDER_CACHE
#include "file.h"
#include "token.h"
#include "log.h"
#include "resource.h"
#include "incToken.h"
#include "project.h"
#include "foo.h"
#include "timer.h"
#include "FileTypes.h"
#include "StringUtils.h"
#include "wt_stdlib.h"
#include "Settings.h"
#include "DevShellAttributes.h"
#include "Directories.h"
#include "fdictionary.h"
#include "RegKeys.h"
#include "Registry.h"
#include <wchar.h>
#include "FileList.h"
#include "TokenW.h"
#include "ProjectInfo.h"
#include "VASeException\VASeException.h"
#include <vector>
#include "CodeGraph.h"
#include "..\common\ThreadStatic.h"
#include "GetFileText.h"
#include "crypto++\sha.h"
#include "LogElapsedTime.h"
#include "SemiColonDelimitedString.h"
#include <ShObjIdl.h>
#include <filesystem>
#include <stack>
#include "VAAutomation.h"
#include <experimental/generator>
#include "VAHashTable.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MSC_VER > 1200
using std::ios;
using std::ofstream;
#endif

BOOL gIgnoreRestrictedFileType = FALSE; // [case: 147774] override restricted file type for ini file

static CCriticalSection sFileUtilLock;
static CStringW sFrameworkDirs;
static CStringW sTempDir0, sTempDir1, sTempDir2;
static CStringW sVaDir1, sVaDir2, sVaDir3, sVaDir5;


template<typename TRAITS>
bool ValidatePath(const CStringT<wchar_t, TRAITS>& path)
{
	/*
	    Nice blog post re: path canonicalization at:
	    http://pdh11.blogspot.com/2009/05/pathcanonicalize-versus-what-it-says-on.html
	    see also: http://stackoverflow.com/questions/1816691/how-do-i-resolve-a-canonical-filename-in-windows

	    Examples paths for the same file:
	    D:\Music\Fools Gold.flac						� Probably canonical
	    D:/Music/Fools Gold.flac						� Slash versus backslash
	    D:\MUSIC\Fools Gold.flac						� Case-insensitive per locale
	    D:\Music\FOOLSG~1.FLA							� MS-DOS 8.3
	    M:\Fools Gold.flac								� After �subst M: D:\Music�
	    \Device\HarddiskVolume2\Music\Fools Gold.flac	� If D: is local
	    \\server\share\Music\Fools Gold.flac			� If D: is a network drive
	    \\?\UNC\server\share\Music\Fools Gold.flac		� Or like this
	    \\?\D:\Music\Fools Gold.flac					� Ultra-long-filenames mode
	    \\.\D:\Music\Fools Gold.flac					� Device namespace
	    \\?\UNC\D:\Music\Fools Gold.flac				� Allegedly
	    \\?\Volume{GUID}\Music\Fools Gold.flac			� Crikey
	 */

	if (-1 != path.FindOneOf(L"*?\"<>|"))
		return false;

	int slashslashPos = path.Find(L"//");
	if (-1 == slashslashPos)
		slashslashPos = path.Find(L"\\\\");
	if (-1 != slashslashPos)
	{
		if (0 == slashslashPos)
		{
			// ok:		//foo/bar (linux)
			// ok:		\\foo\bar (win UNC)
			if (path.GetLength() < 4 || 2 == path.Find(L'.') || (path[3] == L'\\' && path.Find(L"\\\\") == 0))
			{
				// not ok:	\\.\ (mailslot per logs in case=18867)
				Log((const char*)(CString("InvalidPath: ") + CString(path)));
				return false;
			}
			else
			{
				const int spacePos = path.Find(L' ');
				if (spacePos != -1)
				{
					const int secondSlashPos = path.Find(L'/', 2);
					const int secondSlashPos2 = path.Find(L'\\', 2);
					if (secondSlashPos2 == -1 && secondSlashPos == -1)
					{
						// [case=18656]
						// not ok:	// this is a comment
						Log((const char*)(CString("InvalidPath: ") + CString(path)));
						return false;
					}

					if (secondSlashPos2 == -1 && spacePos < secondSlashPos)
					{
						// [case=18656]
						// not ok:	// 07/13
						Log((const char*)(CString("InvalidPath: ") + CString(path)));
						return false;
					}

					if (secondSlashPos == -1 && spacePos < secondSlashPos2)
					{
						// [case=18656]
						// not ok:	// 07\13
						Log((const char*)(CString("InvalidPath: ") + CString(path)));
						return false;
					}

					if (secondSlashPos2 != -1 && secondSlashPos != -1)
					{
						if (spacePos < secondSlashPos && spacePos < secondSlashPos2)
						{
							// [case=18656]
							// not ok:	// 07\13/12
							Log((const char*)(CString("InvalidPath: ") + CString(path)));
							return false;
						}
					}

					// [case: 21097]
					// ok:		\\gr-s01\GlobalRisk Development\Release 610\Projects\GRC DLL\GRCAnalysis\Ana-Quote.cpp
				}
			}

			// [case: 57973] back-door to just completely disallow UNC
			// [case: 73552] only reject if settings have actually been loaded
			if (Psettings && !Psettings->mAllowUncPaths)
			{
				Log((const char*)(CString("disallow UNC filepath: ") + CString(path)));
				return false;
			}
			else
			{
				Log((const char*)(CString("allow UNC filepath: ") + CString(path)));
			}
		}
		else
		{
			// ok:		c:/foo//bar
			// ok:		c:\foo\\bar
			if (path.GetLength() < 4 || !iswalpha(path[3]) || 2 == slashslashPos)
			{
				// not ok:	c:\\foo\bar
				// not ok:	c://foo/bar
				Log((const char*)(CString("InvalidPath: ") + CString(path)));
				return false;
			}
		}
	}
	return true;
}
template bool ValidatePath<>(const CStringW&);
template bool ValidatePath<>(const FFStringW&);


template<typename TRAITS>
CStringT<wchar_t, TRAITS> _MSPath(const CStringT<wchar_t, TRAITS>& path)
{
	DEFTIMERNOTE(MSPathTimer, CString(path));
	CStringT<wchar_t, TRAITS> retval;
	if (!ValidatePath(path))
		return retval;

	WCHAR full[MAX_PATH + 1];
	LPWSTR fname;
	if (GetFullPathNameW(path, MAX_PATH, full, &fname) <= 0)
	{
		// what to do if no file?
		// returning old val...
		retval = path;
		retval.Replace(L'/', L'\\');
		vLogUnfiltered("ERROR MSPath %s", (LPCTSTR)CString(path));
		return retval;
	}

	retval = full;
	return retval;
}
CStringW MSPath(const CStringW& path)
{
	return _MSPath(path);
}

CStringW Pwd()
{
	const int kLen = 512;
	CStringW retval;
	WCHAR* buf = retval.GetBuffer(kLen);
	_wgetcwd(buf, kLen);
	// return path w/o ending backslash, getcwd can return c:\ and we append a \foo to make it c:\\foo
	size_t i = wcslen(buf);
	if (i && buf[i - 1] == L'\\')
		buf[i - 1] = L'\0';
	retval.ReleaseBuffer();
	return retval;
}

int GetFileTypeByExtension(LPCTSTR s)
{
	return GetFileTypeByExtension(CStringW(s));
}

int GetFileTypeByExtension(const CStringW& s)
{
	return GetFileType(s, true);
}

int GetFileType(LPCTSTR file)
{
	return GetFileType(CStringW(file));
}

int GetFileType(const CStringW& file, bool requireExtension /*= false*/, bool deepInspect /*= false*/)
{
	if (!Psettings)
		return Other;
	if (file.IsEmpty())
		return Other;

	CStringW s = file;
	for (int i = 0; i < s.GetLength(); i++)
	{
		wchar_t ch = ((const wchar_t *)s)[i];
		if((ch >= 'A') && (ch <= 'Z'))
		{
			s.MakeLower();
			break;
		}
	}
	const WCHAR* pp = L"";

	// path may contain '.../.NET/...', get real extension
	for (LPCWSTR p = s; *p; p++)
	{
		if (*p == L'.')
			pp = p;
		if (*p == L'/' || *p == L'\\')
			pp = L"";
	}

	if (!(pp && *pp))
	{
		// extensionless file
		if (IsCFile(gTypingDevLang) && Psettings->mExtensionlessFileIsHeader)
		{
			if (s.Find(L"\\\\") == 0)
			{
				// [case: 18867] this might be a comment - watch out for IsFile on comment
				// [case: 91605] This could break valid extensionless headers in unc paths;
				if (s.Find(L"\\\\.") == 0)
				{
					Log((const char*)(CString("warn: GFT:InvalidPath (slot?): ") + CString(file)));
					return Other;
				}

				if (-1 != s.FindOneOf(L" \t"))
				{
					Log((const char*)(CString("warn: GFT:InvalidPath (text?): ") + CString(file)));
					return Other;
				}
			}

			if (s.Find(L"//") == 0)
			{
				// [case: 18867] this might be a comment - watch out for IsFile on comment
				// This will break valid extensionless headers in unc paths;
				// might think about lessening that chance by checking for spaces in the string?
				Log((const char*)(CString("warn: GFT:InvalidPath (comment?): ") + CString(file)));
				return Other;
			}

			// [case: 91632]
			if (requireExtension)
				return Header;
		}

		// [case: 25168] do not hit disk if extension is required
		// requireExtension is used to prevent the call to IsFile down below
		if (requireExtension)
		{
			if (s.Find(L"include") != -1 || s.Find(L"stl") != -1 || s.Find(L"header") != -1 || s.Find(L"boost") != -1)
				return Header;

			return Other;
		}

		if (((IsCFile(gTypingDevLang) && Psettings->mExtensionlessFileIsHeader) || s.Find(L"include") != -1 ||
		     s.Find(L"stl") != -1 || s.Find(L"header") != -1 || s.Find(L"boost") != -1) &&
		    IsFile(s))
			return Header; // STD lib's don't have extensions

		return Other;
	}

	static thread_local CString fExt;
	fExt = pp;
	fExt += ";";
	// dont want to rebuild both sides each time we add a new type to settings
	if (_tcsstr(".uci;.uc;", fExt))
	{
		if (Psettings->mUnrealScriptSupport)
			return UC;
		else
			return Other; // Not supported in non UC builds
	}
	if (_tcsstr(Psettings->m_ShaderExts, fExt))
	{
		if (Psettings->mEnableShaderSupport)
			return Header;
		else
			return Other;
	}
	if (Psettings->mEnableCudaSupport)
	{
		// [case: 58450]
		if (_tcsstr(".cuh;", fExt))
			return Header;
		else if (_tcsstr(".cu;", fExt))
			return Src;
	}
	if (_tcsstr(Psettings->m_hdrExts, fExt))
		return Header;
	if (_tcsstr(Psettings->m_resExts, fExt))
		return RC;

	// following comment added by Jer between June 2002 and Mar 2003
	// (not present in va.4 but there at the start in trunk)
	// jer's comment: do this last because it contains all msdev types source, header, rc, ...
	// can't find a reason for it unless it has something to do with only vc6.
	// ignoring it and moving the compare back closer to the start.
	if (_tcsstr(Psettings->m_srcExts, fExt))
		return Src;
	if (_tcsstr(Psettings->m_plainTextExts, fExt))
		return Plain;

	if (_tcsstr(Psettings->m_idlExts, fExt))
		return Idl;
	if (_tcsstr(Psettings->m_binExts, fExt))
		return Binary;
	if (_tcsstr(Psettings->m_javExts, fExt))
		return Java;
	if (_tcsstr(Psettings->m_phpExts, fExt))
		return PHP;
	if (_tcsstr(Psettings->m_jsExts, fExt))
		return JS;
	if (_tcsstr(Psettings->m_csExts, fExt))
		return CS;

	// group HTML like together - before HTML
	if (_tcsstr(Psettings->m_xamlExts, fExt))
		return XAML;
	if (_tcsstr(Psettings->m_xmlExts, fExt))
		return XML;
	if (_tcsstr(Psettings->m_aspExts, fExt))
		return ASP;
	if (_tcsstr(Psettings->m_htmlExts, fExt))
		return HTML;

	// put vbs before vb
	if (_tcsstr(Psettings->m_vbsExts, fExt))
		return VBS;
	if (_tcsstr(Psettings->m_vbExts, fExt))
		return VB;

	if (_tcsstr(Psettings->m_perlExts, fExt))
		return PERL;
	if (_tcsstr(".sql;", fExt))
		return SQL;
	if (_tcsstr(".bmp;.cur;.img;.ico;.png;.jpg;.jpeg;.tif;.tiff;.gif;", fExt))
		return Image;
	if (_tcsicmp(".prt;", fExt) == 0)
		return Tmp;
	if (_tcsstr(".md;.markdown;", fExt))
		return Plain; //  [case: 116064]

	if (deepInspect)
	{
		// when deepInspect is set, returning XML instead of Other will not
		// affect parse or completion lists
		if (_tcsstr(".css;.wav;.flac;.mp3;.mpg;.mpeg;.avi;.flv;.swf;", fExt))
			return Other;

		if (IsLikelyXmlFile(file))
			return XML;
	}

	return Other;
}

void Append(const CStringW& from, const CStringW& to)
{
	CLEARERRNO;
	WTofstream ofs(to, ios::app);

	{
		WTString fromContent;
		fromContent.ReadFile(from, -1, true);
		ofs << fromContent.c_str();
	}

	if (!ofs)
		Log(FormatStr(IDS_FILE_WRITE_ERROR, (LPCTSTR)CString(to), ERRORSTRING).c_str());
}

WTString ReadFile(const CStringW& file)
{
	DEFTIMERNOTE(ReadFileTimer, CString(file));
	if (!ValidatePath(file))
		return NULLSTR;
	if (g_CodeGraphWithOutVA)
		return GetFileText(file);
	WTString retVal;
	retVal.ReadFile(file);
	return retVal;
}

bool ReadFileUtf16(const CStringW& filename, CStringW& contents)
{
	DWORD err;
	int retry = 10;
	contents.Empty();

againUtf16:
	HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                           FILE_ATTRIBUTE_NORMAL, NULL);
	if (!hFile || hFile == INVALID_HANDLE_VALUE)
	{
		err = GetLastError();
		if (retry && err == 0x20)
		{
			retry--;
			Sleep(250);
			goto againUtf16;
		}
		vLog("ERROR ReadFileU: reading %s %ld", (LPCTSTR)CString(filename), err);
		return false;
	}

	DWORD fsLow, fsHi;
	fsLow = GetFileSize(hFile, &fsHi);
	if (fsLow == 0xFFFFFFFF && (err = GetLastError()) != NO_ERROR)
	{
		CloseHandle(hFile);
		vLog("ERROR ReadFileU: 2 reading %s %ld", (LPCTSTR)CString(filename), err);
		_ASSERTE(!"ERROR ReadFileU: 2");
		return false;
	}
	else if (fsHi)
	{
		CloseHandle(hFile);
		vLog("ERROR ReadFileU: reading huge file %s", (LPCTSTR)CString(filename));
		_ASSERTE(!"ERROR ReadFileU:");
		// return or read as much as we can??
		return false;
	}

	bool retval = false;
	LPWSTR pData = contents.GetBufferSetLength(int((fsLow + sizeof(WCHAR)) / sizeof(WCHAR)));
	if (ReadFile(hFile, pData, fsLow, &fsHi, NULL))
	{
		pData[fsHi / sizeof(WCHAR)] = L'\0';
		retval = true;

		const char* pChars = (const char*)pData;
		// ReadFileUtf16 assumes no BOM - if this assert fires, then use ReadFileW
		_ASSERTE(fsHi < 3 || (pChars[0] != '\xef' && pChars[1] != '\xbb' && pChars[2] != '\xbf'));
		_ASSERTE(!fsHi || (pData[0] != (WCHAR)0xfffe && pData[0] != (WCHAR)0xfeff));
		contents.ReleaseBufferSetLength(int(fsHi / sizeof(WCHAR)));
		std::ignore = pChars;
	}
	else
	{
		err = GetLastError();
		vLog("ERROR ReadFileU: 3 reading %s %ld", (LPCTSTR)CString(filename), err);
		_ASSERTE(!"ERROR ReadFileU: 3");
		contents.ReleaseBufferSetLength(0);
	}

	CloseHandle(hFile);
	return retval;
}

bool ReadFileW(const CStringW& filename, CStringW& contents, int maxAmt /*= -1*/)
{
	DWORD err;
	int retry = 50;
	contents.Empty();

again:
	HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                           FILE_ATTRIBUTE_NORMAL, NULL);
	if (!hFile || hFile == INVALID_HANDLE_VALUE)
	{
		err = GetLastError();
		if (retry && err == 0x20)
		{
			retry--;
			vLog("WARN ReadFileW: retry read %s %ld", (LPCTSTR)CString(filename), err);
			Sleep(50);
			goto again;
		}
		vLog("ERROR ReadFileW: reading %s %ld", (LPCTSTR)CString(filename), err);
		return false;
	}

	DWORD fsLow, fsHi;
	fsLow = GetFileSize(hFile, &fsHi);
	if (fsLow == 0xFFFFFFFF && (err = GetLastError()) != NO_ERROR)
	{
		CloseHandle(hFile);
		vLog("ERROR ReadFileW: 2 reading %s %ld", (LPCTSTR)CString(filename), err);
		_ASSERTE(!"ERROR ReadFileW: 2");
		return false;
	}
	else if (fsHi)
	{
		CloseHandle(hFile);
		vLog("ERROR ReadFileW: reading huge file %s", (LPCTSTR)CString(filename));
		_ASSERTE(!"ERROR ReadFileW:");
		// return or read as much as we can??
		return false;
	}

	if (maxAmt != -1 && fsLow > (DWORD)maxAmt)
		fsLow = (DWORD)maxAmt;

	bool retval = false;
	byte* pData = new byte[fsLow + sizeof(WCHAR)];
	if (ReadFile(hFile, pData, fsLow, &fsHi, NULL))
	{
		pData[fsHi] = '\0';
		pData[fsHi + 1] = '\0'; // in case is wide
		retval = true;

		char* pChars = (char*)pData;
		WCHAR* pWChars = (WCHAR*)pData;
		if (fsHi > 1 && pWChars[0] == (WCHAR)0xfffe)
		{
			// UTF16 BE: turn into UTF16 LE by swapping bytes
			_swab(pChars, pChars, (int)fsHi);
		}

		if (fsHi > 1 && pWChars[0] == (WCHAR)0xfeff)
		{
			// UTF-16 LE
			fsHi -= sizeof(WCHAR);
			wmemmove(pWChars, &pWChars[1], fsHi / sizeof(WCHAR));
			const CStringW tmp(pWChars, int(fsHi / sizeof(WCHAR)));
			contents = tmp;
		}
		else if (fsHi > 2 && pChars[0] == '\xef' && pChars[1] == '\xbb' && pChars[2] == '\xbf')
		{
			// UTF8 w/ BOM
			const WTString tmp(&pChars[3], int(fsHi - 3));
			contents = tmp.Wide();
		}
		else if (fsHi)
		{
			DWORD cpBehavior = Psettings->mFileLoadCodePageBehavior;
			if (Settings::FLCP_AutoDetect == cpBehavior)
			{
				if (CanReadAsUtf8(pChars))
					cpBehavior = Settings::FLCP_ForceUtf8;
				else
					cpBehavior = Settings::FLCP_ACP;
			}

			_ASSERTE(cpBehavior != Settings::FLCP_AutoDetect);
			if (Settings::FLCP_ForceUtf8 == cpBehavior)
			{
				// 7-bit Ascii or utf8 w/o BOM
				const WTString tmp(pChars, (int)fsHi);
				contents = tmp.Wide();
			}
			else
			{
				// not utf8, use ACP or user-defined codepage
				contents =
				    MbcsToWide(pChars, (int)fsHi, int(Settings::FLCP_ACP == cpBehavior ? ::GetACP() : cpBehavior));
			}
		}
	}
	else
	{
		err = GetLastError();
		vLog("ERROR ReadFileW: 3 reading %s %ld", (LPCTSTR)CString(filename), err);
		_ASSERTE(!"ERROR ReadFileW: 3");
	}

	delete[] pData;
	CloseHandle(hFile);
	return retval;
}

WTString Basename(const WTString& file)
{
	const int pos1 = file.ReverseFind('/');
	const int pos2 = file.ReverseFind('\\');
	if (-1 == pos1 && -1 == pos2)
		return file;

	const int snipPos = max(pos1, pos2);
	WTString rstr(file.Mid(snipPos + 1));
	return rstr;
}

CStringW Basename(const CStringW& file)
{
	const int pos1 = file.ReverseFind(L'/');
	const int pos2 = file.ReverseFind(L'\\');
	if (-1 == pos1 && -1 == pos2)
		return file;

	const int snipPos = max(pos1, pos2);
	CStringW rstr(file.Mid(snipPos + 1));
	return rstr;
}

CStringW Path(const CStringW& file)
{
	const int pos1 = file.ReverseFind(L'/');
	const int pos2 = file.ReverseFind(L'\\');
	if (-1 == pos1 && -1 == pos2)
		return file;

	int snipPos = max(pos1, pos2);
	if (snipPos == 2 && file[1] == L':')
	{
		if (++snipPos >= file.GetLength())
			return file;
	}
	CStringW rstr(file.Left(snipPos));
	return rstr;
}

bool IsFile(LPCTSTR file)
{
	DEFTIMERNOTE(IsFileTimer, file);
	if (!ValidatePath(CStringW(file)))
		return false;

	// confirm that it is a file and not a directory
	DWORD attr = GetFileAttributesA(file);
	if (INVALID_FILE_ATTRIBUTES == attr || (attr & FILE_ATTRIBUTE_DIRECTORY))
		return false; //	not a file
	return true;
}

template<typename TRAITS>
bool IsFile(const CStringT<wchar_t, TRAITS>& file, dir_cache_stats_ptr dcs)
{
	DEFTIMERNOTE(IsFileTimerW, CString(file));
	if (!ValidatePath(file))
		return false;

	if(dcs)
		++dcs->IsFile_cnt;

	// confirm that it is a file and not a directory
	DWORD attr = GetFileAttributesW(file);
	if (INVALID_FILE_ATTRIBUTES == attr || (attr & FILE_ATTRIBUTE_DIRECTORY))
		return false; //	not a file
	return true;
}
bool IsFile(const CStringW& file)
{
	return IsFile(file, nullptr);
}

bool IsDir(LPCWSTR dir)
{
	DEFTIMERNOTE(IsDirTimer, CString(dir));
	if (!ValidatePath(CStringW(dir)))
		return false;
	// confirm that it is a directory and not a file
	DWORD attr = GetFileAttributesW(dir);
	if (INVALID_FILE_ATTRIBUTES == attr || !(attr & FILE_ATTRIBUTE_DIRECTORY))
		return false; //	not a directory
	return true;
}

bool CreateDir(const CStringW& dir)
{
	DEFTIMERNOTE(CreateDirTimer, CString(dir));
	if (IsDir(dir))
		return true;

	struct _stat fileStat;
	if (_wstat(dir, &fileStat) == -1)
	{
#if defined(VA_CPPUNIT)
		constexpr int kSleepAmount = 1000;
		constexpr int kRetryCount = 60;
#else
		constexpr int kSleepAmount = 500;
		constexpr int kRetryCount = 20;
#endif
		int res = 0;
		for (int retry = kRetryCount; retry; --retry)
		{
			res = SHCreateDirectoryExW(nullptr, dir, nullptr);
			if (ERROR_SUCCESS == res)
				return true;

			if (ERROR_ALREADY_EXISTS == res)
				return true;

			Sleep(kSleepAmount);
		}

		if (ERROR_FILE_EXISTS == res)
			_ASSERTE(!"CreateDir fail after multiple retries -- ERROR_FILE_EXISTS");
		else
			_ASSERTE(!"CreateDir fail after multiple retries");

		return false;
	}
	else if (fileStat.st_mode & _S_IFDIR)
	{
		return true;
	}

	_ASSERTE(!"CreateDir fail");
	return false;
}

bool IsFileReadOnly(const CStringW& file)
{
	if (!ValidatePath(file))
		return false;
	struct _stat fileStat;
	if (_wstat(file, &fileStat) == -1)
		return true; //	not a file
	else if (fileStat.st_mode & _S_IWRITE)
	{
		return false; // writeable
	}
	return true;
}

bool SetFTime(const CStringW& file, FILETIME* ft)
{
	if (!ValidatePath(file))
		return false;
	bool retval = true;
	HANDLE fp = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                        FILE_ATTRIBUTE_ARCHIVE, NULL);
	if (INVALID_HANDLE_VALUE == fp)
	{
		if (g_loggingEnabled)
		{
			if (IsFile(file))
			{
				vLog("ERROR: SetFTime unable to create file %s", (LPCTSTR)CString(file));
			}
			else
			{
				vLog("WARN: SetFTime called on non-existent file %s", (LPCTSTR)CString(file));
			}
		}
		ASSERT(FALSE);
		return false;
	}
	FILETIME creation, lastAccess;
	WTGetFileTime(fp, &creation, &lastAccess, NULL);
	if (!SetFileTime(fp, &creation, &lastAccess, ft))
	{
		vLog("ERROR: SetFTime unable to settime %s 0x%08lx", (LPCTSTR)CString(file), GetLastError());
		ASSERT(FALSE);
		retval = false;
	}
	CloseHandle(fp);
	return retval;
}

bool SetFTime(const CStringW& file, const CStringW& org)
{
	if (!ValidatePath(file))
		return false;
	bool retval = true;
	HANDLE fp = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                        FILE_ATTRIBUTE_ARCHIVE, NULL);
	if (INVALID_HANDLE_VALUE == fp)
	{
		if (g_loggingEnabled)
		{
			if (IsFile(file))
			{
				vLog("ERROR: SetFTime unable to create file %s", (LPCTSTR)CString(file));
			}
			else
			{
				vLog("WARN: SetFTime called on non-existent file %s", (LPCTSTR)CString(file));
			}
		}
		_ASSERTE(FALSE);
		return false;
	}
	HANDLE fporg = CreateFileW(org, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                           FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == fporg)
	{
		if (IsFile(org))
		{
			vLog("ERROR: SetFTime unable to create file %s", (LPCTSTR)CString(org));
			ASSERT(FALSE);
		}
		CloseHandle(fp);
		return false;
	}
	FILETIME creation, lastAccess, lastWrite;
	WTGetFileTime(fporg, &creation, &lastAccess, &lastWrite);
	if (!SetFileTime(fp, &creation, &lastAccess, &lastWrite))
	{
		vLog("ERROR: SetFTime unable to settime %s 0x%08lx", (LPCTSTR)CString(file), GetLastError());
		ASSERT(FALSE);
		retval = false;
	}
	CloseHandle(fporg);
	CloseHandle(fp);
	return retval;
}

bool TouchFile(const CStringW& file)
{
	if (IsFile(file))
	{
		FILETIME ft;
		::GetSystemTimeAsFileTime(&ft);
		return SetFTime(file, &ft);
	}
	return false;
}

BOOL IsWriteable(const CStringW& file)
{
	struct _stat statbuf;
	if (!ValidatePath(file))
		return FALSE;
	_wstat(file, &statbuf);
	return (statbuf.st_mode & S_IWRITE);
}

DWORD GetFSize(const CStringW& file)
{
	if (!ValidatePath(file))
		return 0;
	HANDLE fp = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                        FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == fp && IsFile(file))
	{
		// try again
		Sleep(20);
		fp = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		                 FILE_ATTRIBUTE_NORMAL, NULL);
	}

	if (INVALID_HANDLE_VALUE == fp)
	{
		if (g_loggingEnabled)
		{
			if (IsFile(file))
			{
				vLog("ERROR: GetFSize unable to create file %s", (LPCTSTR)CString(file));
			}
			else
			{
				vLog("WARN: GetFSize called on non-existent file %s", (LPCTSTR)CString(file));
			}
		}
		return 0;
	}
	DWORD sz = GetFileSize(fp, NULL);
	if (sz == (unsigned)-1)
	{
		vLog("ERROR: GetFSize %s 0x%08lx", (LPCTSTR)CString(file), GetLastError());
	}
	CloseHandle(fp);
	return sz;
}

bool GetFTime(const CStringW& file, FILETIME* ft)
{
	DEFTIMERNOTE(GetFTimeTimer, CString(file));
	if (!ValidatePath(file))
		return 0;

	DWORD t1 = GetTickCount();
	if (!file || !file[0])
	{
		ft->dwHighDateTime = ft->dwLowDateTime = 0;
		return false;
	}

	HANDLE fp = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	                        FILE_ATTRIBUTE_NORMAL, NULL);

	if (INVALID_HANDLE_VALUE == fp && IsFile(file))
	{
		// try again
		Sleep(20);
		fp = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		                 FILE_ATTRIBUTE_NORMAL, NULL);
	}

	if (INVALID_HANDLE_VALUE == fp)
	{
		if (IsFile(file))
		{
			vLog("ERROR: GetFTime unable to create file %s", (LPCTSTR)CString(file));
		}
		else
		{
			vLog("WARN: GetFTime called for file that doesn't exist %s", (LPCTSTR)CString(file));
		}
		ft->dwHighDateTime = ft->dwLowDateTime = 0;
		DWORD t2 = GetTickCount();
		if ((t2 - t1) > 1000)
			VALOGERROR(((LPCTSTR)(CString("WARN: GFT1 lag: ") + CString(file))));
		return false;
	}

	if (!WTGetFileTime(fp, NULL, NULL, ft))
	{
		vLog("ERROR: GetFTime %s 0x%08lx", (LPCTSTR)CString(file), GetLastError());
		_ASSERTE(FALSE);
		CloseHandle(fp);
		ft->dwHighDateTime = ft->dwLowDateTime = 0;
		DWORD t2 = GetTickCount();
		if ((t2 - t1) > 1000)
			VALOGERROR((CString("WARN: GFT2 lag: ") + CString(file)));
		return false;
	}
	CloseHandle(fp);

	DWORD t2 = GetTickCount();
	if ((t2 - t1) > 1000)
		VALOGERROR((CString("WARN: GFT3 lag: ") + CString(file)));
	return true;
}

bool FileTimesAreEqual(const CStringW& f1, const CStringW& f2)
{
	FILETIME ft1, ft2;
	GetFTime(f1, &ft1);
	GetFTime(f2, &ft2);
	return FileTimesAreEqual(&ft1, &ft2);
}

bool FileTimesAreEqual(const FILETIME* ft1, const CStringW& f2)
{
	FILETIME ft2;
	GetFTime(f2, &ft2);
	return FileTimesAreEqual(ft1, &ft2);
}

bool Copy(const CStringW& from, const CStringW& to, bool SaveTime)
{
	if (!CopyFileW(from, to, FALSE))
		return false;
	if (SaveTime)
		SetFTime(to, from);
	return true;
}

int CleanDir(CStringW searchDir, LPCWSTR spec)
{
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	CStringW searchSpec;
	CStringW fileAndPath;

	const WCHAR kLastChar = searchDir[searchDir.GetLength() - 1];
	if (kLastChar == L'\\' || kLastChar == L'/')
		searchDir = searchDir.Left(searchDir.GetLength() - 1);

	if (searchDir.IsEmpty() || !IsDir(searchDir))
		return 1;

	searchSpec = searchDir + L"\\" + spec;
	hFile = FindFirstFileW(searchSpec, &fileData);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		do
		{
			fileAndPath = searchDir + L"\\" + fileData.cFileName;
			DeleteFileW(fileAndPath);
		} while (FindNextFileW(hFile, &fileData));

		FindClose(hFile);
		return 0;
	}

	return 1;
}

bool FilesAreIdentical(const CStringW& f1, const CStringW& f2)
{
	if (!ValidatePath(f1) || !ValidatePath(f1))
		return FALSE;
	FILETIME ftc1, ftc2, fta1, fta2, ftw1, ftw2;
	HANDLE hf1 = INVALID_HANDLE_VALUE, hf2 = INVALID_HANDLE_VALUE;
	DWORD f1hi, f1lo, f2hi, f2lo;
	bool retval = false;

	__try
	{
		hf1 = CreateFileW(f1, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		                  FILE_ATTRIBUTE_NORMAL, NULL);
		if (INVALID_HANDLE_VALUE == hf1)
		{
			if (g_loggingEnabled)
			{
				if (IsFile(f1))
				{
					vLog("ERROR: FilesAreIdentical unable to create file %ls", (LPCWSTR)f1);
				}
				else
				{
					vLog("WARN: FilesAreIdentical called on non-existent file %ls", (LPCWSTR)f1);
				}
			}
			ASSERT(FALSE);
			__leave;
		}
		hf2 = CreateFileW(f2, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		                  FILE_ATTRIBUTE_NORMAL, NULL);
		if (INVALID_HANDLE_VALUE == hf2)
		{
			if (g_loggingEnabled)
			{
				if (IsFile(f2))
				{
					vLog("ERROR: FilesAreIdentical unable to create file %ls", (LPCWSTR)f2);
				}
				else
				{
					vLog("WARN: FilesAreIdentical called on non-existent file %ls", (LPCWSTR)f2);
				}
			}
			ASSERT(FALSE);
			__leave;
		}

		f1lo = GetFileSize(hf1, &f1hi);
		if (f1lo == (unsigned)-1)
			__leave;
		f2lo = GetFileSize(hf2, &f2hi);
		if (f2lo == (unsigned)-1)
			__leave;
		if (f1lo != f2lo || f1hi != f2hi)
			__leave;

		if (!WTGetFileTime(hf1, &ftc1, &fta1, &ftw1))
			__leave;
		if (!WTGetFileTime(hf2, &ftc2, &fta2, &ftw2))
			__leave;

		if (!(CompareFileTime(&ftc1, &ftc2) || CompareFileTime(&fta1, &fta2) || CompareFileTime(&ftw1, &ftw2)))
			retval = true;
	}
	__finally
	{
		if (hf1 != INVALID_HANDLE_VALUE)
			CloseHandle(hf1);
		if (hf2 != INVALID_HANDLE_VALUE)
			CloseHandle(hf2);
	}
	return retval;
}

void DuplicateFile(const CStringW& from, const CStringW& to)
{
	CopyFileW(from, to, false);
	if (!IsFile(to))
	{
		vLog("ERROR: DuplicateFile unable to create file %s", (LPCTSTR)CString(to));
		ASSERT(false);
		return;
	}
	if (IsWriteable(to) == false)
		_wchmod(to, _S_IREAD | _S_IWRITE); // so that SetFTime works
	SetFTime(to, from);
}

CStringW GetFrameworkDirs()
{
	if (!sFrameworkDirs.IsEmpty())
		return sFrameworkDirs;

	AutoLockCs l(sFileUtilLock);
	if (!sFrameworkDirs.IsEmpty())
		return sFrameworkDirs;

	const CStringW dotNetInstallRoot =
	    GetRegValueW(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\.NETFramework", "InstallRoot");
	if (dotNetInstallRoot.IsEmpty())
	{
		vLog("WARN: FrameworkDirs empty");
		return sFrameworkDirs;
	}

	std::vector<LPCWSTR> versionDirs;

	if (gShellAttr->IsDevenv10OrHigher())
		versionDirs.push_back(L"v4*");
	if (gShellAttr->IsDevenv9OrHigher())
		versionDirs.push_back(L"v3*");
	if (gShellAttr->IsDevenv8OrHigher())
		versionDirs.push_back(L"v2*");
	versionDirs.push_back(L"v1*");

	CStringW tmpDir;
	for (std::vector<LPCWSTR>::const_iterator it = versionDirs.begin(); it != versionDirs.end(); ++it)
	{
		CStringW findStr = dotNetInstallRoot;
		findStr += *it;
		WIN32_FIND_DATAW findFileData;
		HANDLE hFind = FindFirstFileW(findStr, &findFileData);
		if (hFind != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (INVALID_FILE_ATTRIBUTES == findFileData.dwFileAttributes)
					continue;

				if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					// add dir to the list if not "." or ".."
					if (findFileData.cFileName[0] != L'.')
					{
						CStringW curDir(dotNetInstallRoot + findFileData.cFileName + L";");
						tmpDir += curDir;
					}
				}
			} while (FindNextFileW(hFind, &findFileData));
			FindClose(hFind);
		}
	}

	sFrameworkDirs = tmpDir;
	return sFrameworkDirs;
}

int FindFile(CStringW& fileName, const CStringW& extraPath, bool searchLocalPaths, bool searchBinaryPaths, dir_cache_ptr dc, bool* extraPath_used)
{
	// extraPath_used will be false if the positive result is returned regardless of the extraPath parameter
	if (extraPath_used)
		*extraPath_used = false;

	if (fileName.GetLength() && !wcschr(L"\\/", fileName[0]))
	{
		fileName.Replace(L"\\\\", L"\\");
		fileName.Replace(L"//", L"/");
	}
	DEFTIMERNOTE(FindFileTimer, CString(fileName));
	if (fileName.GetLength() > 1 && fileName[1] == L':')
		return IsFile(fileName, dc) ? 1 : -1; // c:...file.h

	CStringW tmp(Basename(fileName));
	tmp.MakeLower();
#ifdef AVR_STUDIO
	// Per Atmel: Never find any <avr[16/32/...]/io.h>, or it will add all device files into db.
	// Per Partha (for AVR 6): Do not parse �sam.h� file, it is same like �io.h�  (it
	//					contains device specific information, we will provide selected
	// 					device�s include file via automation object)
	if (tmp == L"io.h" || tmp == L"sam.h")
		return -1;
#else
	if (tmp == L"xkeycheck.h")
	{
		// [case: 73347]
		return -1;
	}
#endif

	tmp = fileName;
	if (fileName.GetLength() && !wcschr(L"\\/", fileName[0]))
		tmp = L"\\" + tmp;
	// watch out for bogus values that get passed in while typing #includes
	if (fileName.IsEmpty() || fileName[0] == L'\r' || fileName[0] == L'\n' || fileName == L"...")
	{
		vLog("FindFile ERROR arg 1 empty");
		return -1;
	}

	LogElapsedTime tr("FindFile", fileName, 250);
	bool hasLeadingSlash = false;
	if (fileName[0] == L'\\' || fileName[0] == L'/')
		hasLeadingSlash = true;

	if (searchLocalPaths)
	{
		// "file.h"
		// if extraPath is an mfc src dir that contains
		//  #include "stdafx.h", make sure we get its local copy
		//  before getting the current project's copy
		if (extraPath.GetLength() && IsFile(extraPath + tmp, dc))
		{
			fileName = _MSPath(extraPath + tmp); // relative to file

			if (extraPath_used)
				*extraPath_used = true;

			return V_INPROJECT;
		}
		else if (IsFile(fileName, dc))
		{
			fileName = _MSPath(fileName); // absolute path
			return V_INPROJECT;
		}
		else if (IsFile(Pwd() + tmp, dc))
		{
			fileName = _MSPath(Pwd() + tmp); // relative to pwd
			return V_INPROJECT;
		}
		else if (hasLeadingSlash && IsPathRootAbsolute(fileName) && extraPath.GetLength() > 1 && extraPath[1] == L':')
		{
			const CStringW drive = extraPath.Mid(0, extraPath.FindOneOf(L"/\\"));
			if (2 == drive.GetLength()) // c:
			{
				if (IsFile(drive + fileName, dc))
				{
					fileName = drive + fileName;

					if (extraPath_used)
						*extraPath_used = true;

					return V_INPROJECT;
				}
			}
		}

		// not found, search everywhere else
	}

	if (g_CodeGraphWithOutVA)
		return -1;

	{
		DEFTIMERNOTE(FindFileTimer3, CString(fileName));

		auto get_itok = [searchBinaryPaths, searchLocalPaths, dc]() {
			// lazy evaluation of itok variable 

			if(dc)
				++dc->get_itok_cnt;

			CStringW itok;
			IncludeDirs incDirs;
			// add system PATH, LIB, etc
			if (searchBinaryPaths)
			{
				// http://msdn.microsoft.com/en-us/library/yab9swk4.aspx
				itok = GetFrameworkDirs();
				if (GlobalProject)
				{
					const FileList& dirs = GlobalProject->GetUsingDirs();
					for (FileList::const_iterator it = dirs.begin(); it != dirs.end(); ++it)
						itok += (*it).mFilename + L';';
				}
				itok += incDirs.getImportDirs();
			}

			// add our sys includes
			itok += incDirs.getSysIncludes();
			// add our other includes
			itok += incDirs.getAdditionalIncludes();
			// add project additional include dirs
			if (GlobalProject)
			{
				bool addAddlProjDirs = false;
				// don't append proj dirs while building SysDic
				if (g_pMFCDic && g_pMFCDic->m_loaded && g_pCSDic && g_pCSDic->m_loaded)
					addAddlProjDirs = true;
				else if (g_pMFCDic && g_pMFCDic->m_loaded && !g_pCSDic)
					addAddlProjDirs = true;
				else if (g_pCSDic && g_pCSDic->m_loaded && !g_pMFCDic)
					addAddlProjDirs = true;
				else if (!GlobalProject->IsBusy())
					addAddlProjDirs = true;
				else if (g_pMFCDic && g_pMFCDic->m_loaded && g_pCSDic && !GlobalProject->CppUsesClr() &&
				         !GlobalProject->CppUsesWinRT())
				{
					// [case: 64482] project loading w/o .net
					addAddlProjDirs = true;
				}

				if (addAddlProjDirs)
				{
					if (searchLocalPaths)
						itok = GlobalProject->GetProjAdditionalDirs() + L';' + itok;
					else
						itok += GlobalProject->GetProjAdditionalDirs();
				}
			}
			return itok;
		};
		auto iterate_path_set = [](CStringW path_set) -> std::experimental::generator<std::wstring_view> {
			// return semi-colon delimited paths

			SemiColonDelimitedString dirs(path_set);
			LPCWSTR pCurDir;
			int curDirLen;
			while (dirs.NextItem(pCurDir, curDirLen))
				co_yield {pCurDir, (uint32_t)curDirLen};
		};


		std::vector<FFStringW> subpath_components;// normalized dirs + filename at the end
		dir_cache_t::itok_t* cached_itoks = nullptr;
		if (dc)
		{
			// prepare prefix subpaths used in all checks
			int start = 0;
			FFStringW token;
			while (!(token = fileName.Tokenize(L"/\\", start)).IsEmpty())
				subpath_components.emplace_back(dir_t::normalize_filename_c(token));


			// split itok, normalize and cache it; there are 8 different variants possible (searchBinaryPaths, searchLocalPaths and sysdic finished flags) in theory (2 appear in practice)
			bool sysdic_built = (!g_pMFCDic || g_pMFCDic->m_loaded) && (!g_pCSDic || g_pCSDic->m_loaded || (!GlobalProject->CppUsesClr() && !GlobalProject->CppUsesWinRT()));
			auto& [init_once_flag, itoks] = dc->get_cached_itok(searchBinaryPaths, searchLocalPaths, sysdic_built);
			std::call_once(init_once_flag, [&]() {
				CStringW itok = get_itok();
				itoks.reserve(std::count(itok.GetString(), itok.GetString() + itok.GetLength(), L';') + size_t(10)); // + 1 should have been enough, but just in case
				for(std::wstring_view sv : iterate_path_set(itok))
				{
					FFStringW p = to_FFString(sv);
					itoks.emplace_back(dir_t::normalize_path_c(p), p);
				}
			});
			cached_itoks = &itoks;
		}


		bool is_extrapath_set = false;
		auto iterate_path_sets_l = [&]() -> std::experimental::generator<std::tuple<std::wstring_view, std::wstring_view, bool>> {
			// returns all paths, one by one

			if (cached_itoks)
			{
				for (const auto &[s, s_orig_case] : *cached_itoks)
					co_yield {to_string_view(s), to_string_view(s_orig_case), true};
			}
			else
			{
				for (std::wstring_view sv : iterate_path_set(get_itok()))
					co_yield {sv, sv, false};
			}

			if(!extraPath.IsEmpty())
			{
				is_extrapath_set = true;
				for (std::wstring_view sv : iterate_path_set(extraPath))
					co_yield {sv, sv, false};
			}
		};
		auto iterate_path_sets = iterate_path_sets_l();

		FFStringW file;
		for (auto [sv, sv_orig_case, sv_is_normalized] : iterate_path_sets)
		{
			file.SetString(sv.data(), (int)sv.length());

			bool already_found = false;
			if(dc)
			{
				if (subpath_components.empty())
				{
					// just in case; should never happen
					++dc->findfile_fallback1_cnt;
					goto fallback_to_orig_code;
				}

				auto dir = dc->find_dir(file, sv_is_normalized);
				auto spc_it = subpath_components.cbegin();
				while (true)
				{
					if (!dir || (dir->exists == dir_exists_t::no))
						goto path_doesnt_exist;
					if (dir->exists == dir_exists_t::error)
					{
						++dc->findfile_fallback2_cnt;
						goto fallback_to_orig_code;
					}

					if (spc_it == std::prev(subpath_components.cend()))
						break;
					dir = dc->follow_dir(dir, *spc_it++, true);
				}
				if (dir->contains_file(subpath_components.back(), true))
				{
					++dc->findfile_cache_hit_cnt;
					already_found = true; // finish as before, but will skip calling IsFile
				}
				else
				{
path_doesnt_exist:
					++dc->findfile_isfile_avoided_cnt;
					continue;
				}
			}

fallback_to_orig_code:
			if (sv_is_normalized)
				file.SetString(sv_orig_case.data(), (int)sv_orig_case.length()); // have results in the original case

			if (file.GetLength() == 3 && file[1] == L':' && (file[2] == L'/' || file[2] == L'\\'))
			{
				// don't add slash x:/
			}
			else if (!hasLeadingSlash)
				file += L"\\";

			file += fileName;
			if (file.GetLength() && __iswcsym(file[0]))
			{
				file.Replace(L"/", L"\\");
				file.Replace(L"\\\\", L"\\");
			}

			if (already_found || IsFile(file, dc))
			{
				fileName = _MSPath(file);

				if (is_extrapath_set && extraPath_used)
					*extraPath_used = true;

				return 0;
			}
		}

		// for fully qualified path && searchLocalPaths == FALSE
		if (!searchLocalPaths && IsFile(fileName, dc))
		{
			fileName = _MSPath(fileName);
			return 0;
		}
	}

	if (g_loggingEnabled)
	{
		vCatLog("Parser.FileName", "FindFile file not found %s, %d (%s)", (LPCTSTR)CString(fileName), searchLocalPaths,
		     extraPath.GetLength() ? (LPCTSTR)CString(extraPath) : "");
	}
	return -1;
}

void LoadAlternateFilesForGoToDef(const CStringW& /*filepath*/)
{
	// No longer needed?
	// What if someone wants to goto an impl in a file not in their current project?
	// I'm kinda thinking goto just shouldn't support this.

	// 	WTString file = Basename(filepath);
	// 	token2 t(file);
	// 	WTString basename = t.read(".");
	//
	// 	token2 extList(".cpp;2.cpp;.c;.cc;.cxx;.cp;.rc");
	// 	MultiParse mp;
	// 	while(extList.more()>1)
	// 	{
	// 		WTString ext = extList.read(";");
	// 		WTString nfile = basename + ext;
	// 		int findType = FindFile(nfile, Path(file), FALSE, FALSE);
	// 		if (-1 != findType && nfile.GetLength())
	// 		{
	// 			mp.ParseFileForGoToDef(nfile, FALSE);
	// 		}
	// 	}
}

bool SwapExtension(CStringW& fileName, bool searchSysSrcDirs /*= false*/)
{
	DEFTIMERNOTE(SwapExtensionTimer, CString(fileName));
	LogElapsedTime tr("SwapExt", fileName, 250);
	if (fileName.IsEmpty())
		return false;
	const int fType = GetFileType(fileName);
	if (Src != fType && Header != fType)
		return false;

	// if file is right type then switch ext
	const WCHAR* const hdrExts[] = {L".h", L".hpp", L".hh", L".tlh", L".hxx", L".hp", L".rch", NULL};
	const WCHAR* const srcExts[] = {L".cpp", L".c", L".cc", L".tli", L".cxx", L".cp", L".rc", NULL};
	const int kRcExtIdx = 6;
	_ASSERTE(CStringW(L".rc") == srcExts[kRcExtIdx]);
	const bool isInline = fileName.Find(L".inl") == -1 ? false : true;
	CStringW baseName(L"\\" + Basename(fileName));
	const CStringW filePath(Path(fileName));
	int pos = baseName.ReverseFind(L'.');

	IncludeDirs incDirs;
	TokenW itok;
	itok += (filePath + L";");
	itok += (Pwd() + L";");
	if (GlobalProject)
		itok += GlobalProject->GetProjAdditionalDirs();
	itok += incDirs.getAdditionalIncludes();
	itok += incDirs.getSysIncludes();
	if (searchSysSrcDirs)
		itok += incDirs.getSourceDirs();

	if (Psettings && Psettings->m_aggressiveFileMatching)
	{
		// [case: 96426]
		if (Src == fType)
		{
			CStringW p1(filePath + L"\\include"), p2(filePath + L"\\inc");
			if (IsDir(p1))
				itok += p1 + L";";
			if (IsDir(p2))
				itok += p2 + L";";
		}
		else
			itok += (Path(filePath) + L";"); // parent directory if current file is a header
	}

	CStringW lastDitchMatch;

	while (itok.more())
	{
		CStringW newName;
		CStringW thisPath = itok.read(L";");
		if (!thisPath.GetLength())
			continue;

		for (int i = 0; fType == Src ? hdrExts[i] : srcExts[i]; i++)
		{
			newName = thisPath;
			if (pos == -1)
				newName += baseName;
			else
				newName += baseName.Left(pos);
			newName += (fType == Src ? hdrExts[i] : srcExts[i]);
			if (IsFile(newName))
			{
				if (fType != Src && kRcExtIdx == i)
				{
					// only use .rc as a last ditch - continue to look for other files
					lastDitchMatch = newName;
				}
				else
				{
					fileName = newName;
					return true;
				}
			}
		}

		// deal with .inl ext
		if (isInline)
		{
			// this is a .inl file - so try the opposite extensions
			for (int i = 0; fType != Src ? hdrExts[i] : srcExts[i]; i++)
			{
				newName = thisPath;
				newName += baseName.Left(pos);
				newName += (fType != Src ? hdrExts[i] : srcExts[i]);
				if (IsFile(newName))
				{
					fileName = newName;
					return true;
				}
			}
		}
		else
		{
			// try .inl - don't want to add it to the ext lists
			newName = thisPath;
			newName += baseName.Left(pos);
			newName += L".inl";
			if (IsFile(newName))
			{
				fileName = newName;
				return true;
			}
		}
	}

	if (lastDitchMatch.GetLength())
	{
		fileName = lastDitchMatch;
		return true;
	}

	if (Header == fType)
	{
		CStringW newName;
		// metro apps have foo.g.h and foo.g.hpp, so go ahead and check for
		// other headers in same dir
		for (int i = 0; hdrExts[i]; ++i)
		{
			newName = filePath;
			if (pos == -1)
				newName += baseName;
			else
				newName += baseName.Left(pos);
			newName += hdrExts[i];
			if (fileName.CompareNoCase(newName) && IsFile(newName))
			{
				fileName = newName;
				return true;
			}
		}
	}

	if (!Psettings || !Psettings->m_aggressiveFileMatching)
		return false;

	// no match found - try a broader sweep
	FileList dirList;
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;

	baseName = Basename(fileName);
	CStringW nameNoExt(baseName);
	pos = nameNoExt.ReverseFind(L'.');
	if (pos != -1)
		nameNoExt.SetAt(pos, L'\0');

	// go up 1 dir from where file is
	CStringW searchDir(filePath);
	pos = filePath.ReverseFind(L'\\');
	int pos2 = filePath.ReverseFind(L'/');
	if (pos == -1 && pos2 == -1)
		return false;
	if (pos == -1)
		pos = pos2;
	else if (pos2 == -1)
		pos2 = pos;
	pos = max(pos, pos2);
	if (pos > 5) // don't get too close to root
	{
		searchDir.SetAt(pos, L'\0');

		// go up one more dir
		pos = searchDir.ReverseFind(L'\\');
		pos2 = searchDir.ReverseFind(L'/');
		if (pos != -1 || pos2 != -1)
		{
			if (pos == -1)
				pos = pos2;
			else if (pos2 == -1)
				pos2 = pos;
			pos = max(pos, pos2);
			if (pos > 5) // don't get too close to root
				searchDir.SetAt(pos, L'\0');
		}
	}

	// build list of all subdirs under searchDir
	dirList.Add(searchDir);
	FileList::iterator it;
	CStringW dir;
	const DWORD stopCnt = GetTickCount() + 2000;
	// don't spend more than 2 seconds getting dirs
	for (it = dirList.begin(); it != dirList.end() && GetTickCount() < stopCnt; ++it)
	{
		dir = (*it).mFilename;
		CString__FormatW(searchDir, L"%s\\*.*", (LPCWSTR)dir);
		hFile = FindFirstFileW(searchDir, &fileData);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
					continue;

				if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					// add dir to the list if not "." or ".."
					if (fileData.cFileName[0] != L'.')
					{
						// [case: 98947]
						// don't change these casts; operator+ won't work as expected
						dirList.Add(CStringW((LPCWSTR)dir) + L"\\" + fileData.cFileName);
					}
				}
			} while (FindNextFileW(hFile, &fileData));
			FindClose(hFile);

			// protection - just in case
			if (dirList.size() > 100)
				break;
		}
	}

	// search each dir in list
	for (it = dirList.begin(); it != dirList.end(); ++it)
	{
		dir = (*it).mFilename;
		CString__FormatW(searchDir, L"%s\\%s.*", (LPCWSTR)dir, (LPCWSTR)nameNoExt);
		hFile = FindFirstFileW(searchDir, &fileData);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
					continue;

				if (FILE_ATTRIBUTE_DIRECTORY & fileData.dwFileAttributes)
					continue;

				if (baseName.CompareNoCase(fileData.cFileName))
				{
					// don't let .obj files come thru as false positives
					CStringW foundExt(fileData.cFileName);
					pos = foundExt.ReverseFind(L'.');
					if (pos == -1)
						continue;
					foundExt = foundExt.Mid(pos);
					for (int i = 0; hdrExts[i] || srcExts[i]; i++)
					{
						if ((srcExts[i] && !foundExt.CompareNoCase(srcExts[i])) ||
						    (hdrExts[i] && !foundExt.CompareNoCase(hdrExts[i])))
						{
							if (fType != Src || kRcExtIdx != i)
							{
								CString__FormatW(fileName, L"%s\\%s", (LPCWSTR)dir, fileData.cFileName);
								FindClose(hFile);
								return true;
							}
						}
					}
				}
			} while (FindNextFileW(hFile, &fileData));
			FindClose(hFile);
		}
	}

	return false;
}

void FilterFileMatches(const CStringW& filenameToMatch, FileList& matches)
{
	// remove filenameToMatch from results list
	matches.Remove(filenameToMatch);
	if (matches.empty())
		return;

	if (!Psettings->mClearNonSourceFileMatches)
		return;

	const int kFtypeOfFileToMatch = GetFileType(filenameToMatch);
	// remove non-source files from results list
	FileList itemsToRemove;
	FileList::iterator it;
	for (it = matches.begin(); it != matches.end(); ++it)
	{
		const CStringW curFile((*it).mFilename);
		const int fType = GetFileType(curFile);
		bool exclude = false;

		if (Is_Some_Other_File(kFtypeOfFileToMatch))
		{
			// [case: 101997]
			// don't exclude any others if the active file is unknown type
		}
		else if (IsCFile(kFtypeOfFileToMatch))
		{
			if (!IsCFile(fType))
			{
				if (GlobalProject && GlobalProject->CppUsesWinRT() && XAML == fType)
					; // [case: 61837]
				else
					exclude = true; // maintain old behavior for C
			}
		}
		else if (XAML == kFtypeOfFileToMatch && IsCFile(fType) && GlobalProject && GlobalProject->CppUsesWinRT())
			; // [case: 61837]
		else if (fType != CS && !Is_VB_VBS_File(fType) && !Is_Tag_Based(fType) && fType != JS && fType != Other)
		{
			// foo.cs / foo.cs.aspx / foo.asp - allow multiple languages in single result set (but not C)
			// [case: 13740] added Other for xaml support
			exclude = true;
		}

		if (exclude)
			itemsToRemove.Add(curFile);
	}

	for (it = itemsToRemove.begin(); it != itemsToRemove.end(); ++it)
		matches.Remove((*it).mFilename);
}

void FilterExcessMatches(const CStringW& filenameToMatch, FileList& matches)
{
	if (matches.size() < 2)
		return;

	// [case: 141728]
	// filter partial matches if any exact matches are found once all
	// file extensions are eliminated from consideration
	CStringW basebase(::GetBaseNameNoExt(filenameToMatch));
	while (-1 != basebase.Find(L'.'))
		basebase = ::GetBaseNameNoExt(basebase);

	FileList itemsToRemove;
	int exactMatches = 0;
	int partialMatches = 0;
	for (const auto& it : matches)
	{
		CStringW curFileBase(GetBaseNameNoExt(it.mFilename));
		while (-1 != curFileBase.Find(L'.'))
			curFileBase = ::GetBaseNameNoExt(curFileBase);

		if (curFileBase.CompareNoCase(basebase))
		{
			++partialMatches;
			itemsToRemove.Add(it.mFilename);
		}
		else
			++exactMatches;
	}

	if (!exactMatches)
		return; // if no exact matches, leave results as is

	if (partialMatches == (int)matches.size())
		return; // do not leave empty results

	for (const auto& it : itemsToRemove)
		matches.Remove(it.mFilename);
}

int GetRelatedProjectSourceFiles(const CStringW& filenameToMatch, FileList& matches, bool allowPartialMatch)
{
	LogElapsedTime tr("GetRelatedSourceFiles", filenameToMatch, 250);
	const CStringW filepath(Path(filenameToMatch) + L";");
	const CStringW dirs(GlobalProject->GetProjAdditionalDirs());
	if (!ContainsIW(dirs, filepath))
	{
		// if file to match isn't in a solution directory, don't search
		// the solution.  If user has stdafx.h open for another project,
		// use SwapExtension to locate stdafx.cpp relative to that stdafx.h
		// instead of locating the stdafx files in this solution.
		return -1;
	}

	CStringW basename(GetBaseNameNoExt(filenameToMatch));
	basename.MakeLower();
	if (allowPartialMatch && basename.GetLength())
	{
		// allowPartialMatch can result in non-symmetrical behavior.
		// edcnt2.cpp would locate edcnt.h but not vice versa

		// strip trailing numbers
		CStringW tmpName(basename);
		while (tmpName.GetLength() && wcschr(L"0123456789", tmpName[tmpName.GetLength() - 1]))
			tmpName = tmpName.Left(tmpName.GetLength() - 1);
		if (tmpName.GetLength())
			basename = tmpName;

		// strip common filename suffixes
		// this list could conceivably be read from registry for user customization
		const CStringW kCommonSuffixes[] = {
		    L"iterator", L"base", L"impl", L"priv", L"fwd",
		    // [case: 32515] exp can cause conflicts with regexp - so use _exp;
		    // can't find original case that made me add "exp" anyway
		    L"_exp", L"ex",
		    L"-internal", // google convention
		    L"-inl",      // [case: 84386] google inline convention
		    L"_part",     // [case: 65732]
		    L"model",     // [case: 65732] MyView.xaml / MyView.xaml.cs / MyViewModel.cs
		    L"_p",
		    L"_", // this one should always be second to last
		    L""   // this must always be last - sentry
		};

		for (int lstIdx = 0; !kCommonSuffixes[lstIdx].IsEmpty(); ++lstIdx)
		{
			const CStringW& curSuffix(kCommonSuffixes[lstIdx]);
			const int pos = basename.Find(curSuffix);
			if (-1 != pos && pos == basename.GetLength() - curSuffix.GetLength())
				basename = basename.Left(pos);
		}
	}

	if (allowPartialMatch)
	{
		for (;;)
		{
			GlobalProject->FindBaseFileNameMatches(basename, matches, false);

			// [case: 71335]
			if (basename[0] == _T('i'))
			{
				const size_t cnt = matches.size();
				GlobalProject->FindBaseFileNameMatches(basename.Mid(1), matches, false);
				if (cnt == matches.size())
				{
					// if basename starts with i but isn't the interface (IdeService.cs <-> IIdeService.cs)
					GlobalProject->FindBaseFileNameMatches(_T('i') + basename, matches, false);
				}
			}
			else
				GlobalProject->FindBaseFileNameMatches(_T('i') + basename, matches, false);

			if (matches.size() < 2 && -1 != basename.Find('.'))
			{
				// [case: 73757]
				basename = GetBaseNameNoExt(basename);
			}
			else
				break;
		}
	}
	else
		GlobalProject->FindBaseFileNameMatches(filenameToMatch, basename, matches); // [case: 32543]

	FilterFileMatches(filenameToMatch, matches);

	if (allowPartialMatch)
		FilterExcessMatches(filenameToMatch, matches);

	return (int)matches.size();
}

void GetBestFileMatch(CStringW fname, FileList& possibleMatches)
{
	LogElapsedTime tr("GetBestFileMatch", fname, 250);
	// non-extended search first
	int matchCount = GetRelatedProjectSourceFiles(fname, possibleMatches, false);

	if (!matchCount || -1 == matchCount)
	{
		if (-1 == matchCount)
		{
			// search system dirs for first match
			if (SwapExtension(fname, true) && fname.GetLength())
			{
				matchCount = 1;
				possibleMatches.AddHead(fname);
			}
			return;
		}
		else if (SwapExtension(fname) && fname.GetLength())
		{
			matchCount = 1;
			possibleMatches.AddHead(fname);
		}
		else
		{
			// extended search
			GetRelatedProjectSourceFiles(fname, possibleMatches, true);
			// no further whittling down after extended searches
			return;
		}
	}

	if (1 == matchCount)
		return;

	{
		// [case: 61837] put .xaml files at end of list
		FileList removedFiles;
		for (FileList::iterator it = possibleMatches.begin(); it != possibleMatches.end();)
		{
			const int fType = GetFileType((*it).mFilenameLower);
			if (XAML != fType)
			{
				++it;
				continue;
			}

			removedFiles.Add(*it);
			possibleMatches.remove(*it++);
		}

		for (FileList::iterator it = removedFiles.begin(); it != removedFiles.end(); ++it)
			possibleMatches.Add(*it);
	}

	int fType = GetFileType(fname);
	if (!IsCFile(fType) && fType != CS && !Is_VB_VBS_File(fType) && !Is_Tag_Based(fType) && fType != JS)
		return;

	if (!(matchCount % 2))
		return;

	// special case [case=4372]:
	//		thisDir\foo.h
	//		thisDir\foo.c
	//		thatDir\foo.h
	//		thatDir\foo.c
	// this is only possible when there is an odd number of matches
	// (since this file is not in the match list and I only deal with
	// a set of perfect matches of 1:1 header to source pairs).
	// This is only applicable for non-extended search results returned by
	// GetRelatedProjectSourceFiles.
	const CStringW thisExt(GetBaseNameExt(fname));
	if (!thisExt.CompareNoCase(L"inl"))
		return;

	CStringW directMatch;
	const CStringW thisPath(Path(fname));

	// attempt to locate a directMatch
	bool directMatchWasMade = false;
	int prevType = 0;
	for (FileList::iterator it = possibleMatches.begin(); it != possibleMatches.end(); ++it)
	{
		const CStringW curFile((*it).mFilenameLower);
		const CStringW curExt(GetBaseNameExt(curFile));
		if (curExt == L"inl")
		{
			// punt if any files in the match set are inl
			directMatch.Empty();
			return;
		}

		fType = GetFileType(curFile);
		if (Header == fType)
			fType = Src; // normalize so that Header and Src are same 'language'
		else if (JS == fType || Is_Tag_Based(fType))
			fType = HTML; // normalize so that JS and HTML are same 'language'
		if (prevType && prevType != fType)
		{
			// don't support direct matches if result set contains files of multiple languages
			directMatch.Empty();
			return;
		}
		prevType = fType;

		const CStringW curPath(Path(curFile));
		if (!thisPath.CompareNoCase(curPath))
		{
			if (!directMatch.IsEmpty())
			{
				// punt if there is more than one file in the
				// match that is in the same dir as this file
				directMatch.Empty();
				return;
			}

			const CStringW thisBasenameNoExt(GetBaseNameNoExt(fname));
			const CStringW curBasenameNoExt(GetBaseNameNoExt(curFile));
			if (!thisBasenameNoExt.CompareNoCase(curBasenameNoExt))
			{
				directMatch = curFile;
				directMatchWasMade = true;
			}
		}
	}

	// if a directMatch was found, ensure that the result set contains
	// only perfect matches
	if (!directMatch.IsEmpty())
	{
		const bool requirePerfectMatches = false; // case=8429 - remove perfect set requirement
		if (requirePerfectMatches)
		{
			FileList savedList;
			savedList.AddHead(possibleMatches);

			// remove the directMatch
			possibleMatches.Remove(directMatch);

			// remove all 1:1 matches
			while (possibleMatches.size())
			{
				FileList::iterator it = possibleMatches.begin();
				const CStringW firstFile((*it).mFilenameLower);
				possibleMatches.erase(it);
				const CStringW firstPath(Path(firstFile));
				const CStringW firstBase(GetBaseNameNoExt(firstFile));
				int curMatchCount = 0;
				FileList itemsToRemove;

				for (it = possibleMatches.end(); it != possibleMatches.end(); ++it)
				{
					const CStringW curFile((*it).mFilenameLower);
					const CStringW curBase(GetBaseNameNoExt(curFile));
					const CStringW curPath(Path(curFile));

					if (curPath == firstPath && curBase == firstBase)
					{
						++curMatchCount;
						itemsToRemove.Add((*it).mFilename);
					}
				}

				for (it = itemsToRemove.begin(); it != itemsToRemove.end(); ++it)
					possibleMatches.Remove((*it).mFilenameLower);

				if (1 != curMatchCount)
				{
					// didn't find a 1:1 pair - therefore, not a perfect set - bail
					directMatch.Empty();
					break;
				}
			}

			// at this point, if possibleMatches is not empty, then we did not
			// have a perfect set so restore all original matches
			if (possibleMatches.size() || directMatch.IsEmpty())
			{
				directMatch.Empty();
				possibleMatches.clear();
				possibleMatches.AddHead(savedList);
			}
		}

		if (!directMatch.IsEmpty())
		{
			// we were able to narrow the field down to one file
			possibleMatches.clear();
			possibleMatches.AddHead(directMatch);
		}
	}
	else if (directMatch.IsEmpty() && !directMatchWasMade)
	{
		// no directMatch was made - see if we can use process of
		// elimination to find a match; remove all perfect matches;
		// if only 1 match left, then that's it
		FileList savedList;
		savedList.AddHead(possibleMatches);

		for (int pass = 0; pass < 2 && 1 != possibleMatches.size(); ++pass)
		{
			// remove all 1:1 matches
			size_t possMatchCnt = possibleMatches.size();
			while (possMatchCnt)
			{
				const CStringW firstFile(possibleMatches.begin()->mFilenameLower);
				const CStringW saveFile(possibleMatches.begin()->mFilename);
				possibleMatches.Remove(firstFile);
				--possMatchCnt;
				CStringW firstPath(Path(firstFile));
				if (1 == pass)
				{
					// [case:16697] go up a directory and compare there
					int pos = firstPath.ReverseFind('\\');
					const int pos2 = firstPath.ReverseFind('/');
					pos = max(pos, pos2);
					if (-1 != pos)
						firstPath = firstPath.Left(pos);
					else
						break;
				}
				const CStringW firstBase(GetBaseNameNoExt(firstFile));

				bool anyMatch = false;
				for (FileList::iterator it = possibleMatches.begin(); it != possibleMatches.end();)
				{
					const CStringW curFile((*it).mFilenameLower);
					const CStringW curBase(GetBaseNameNoExt(curFile));
					CStringW curPath(Path(curFile));
					if (1 == pass)
					{
						// [case:16697] go up a directory and compare there
						int pos = curPath.ReverseFind('\\');
						const int pos2 = curPath.ReverseFind('/');
						pos = max(pos, pos2);
						if (-1 != pos)
							curPath = curPath.Left(pos);
						else
							break;
					}

					if (curPath == firstPath && curBase == firstBase)
					{
						anyMatch = true;
						possibleMatches.erase(it++);
						--possMatchCnt;
					}
					else
						++it;
				}

				if (!anyMatch)
					possibleMatches.Add(saveFile);

				if (1 == possibleMatches.size())
					break;
			}

			// if after removing all perfect matches, there is only one file
			// left in the set, then pick that one; otherwise restore all matches
			if (1 != possibleMatches.size())
			{
				possibleMatches.clear();
				possibleMatches.AddHead(savedList);
			}
		}
	}
}

BOOL IsFileTimeNewer(FILETIME* ft1, FILETIME* ft2)
{
	return (ft1->dwHighDateTime > ft2->dwHighDateTime ||
	        (ft1->dwHighDateTime == ft2->dwHighDateTime && ft1->dwLowDateTime > ft2->dwLowDateTime));
}

void FixFileCase(CStringW& file)
{
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	CStringW found;

	hFile = FindFirstFileW(file, &fileData);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		file = Path(file) + L"\\" + fileData.cFileName;
		FindClose(hFile);
	}
}

CStringW FindModifiedFiles(const CStringW& searchDir, const CStringW& spec, FILETIME* since)
{
	DEFTIMERNOTE(FindFilesTimer, CString(searchDir));
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	WCHAR searchSpec[MAX_PATH];
	WCHAR fileAndPath[MAX_PATH];
	CStringW found;
	swprintf_s(searchSpec, L"%s\\%s", (LPCWSTR)searchDir, (LPCWSTR)spec);
	hFile = FindFirstFileW(searchSpec, &fileData);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
				continue;

			if (FILE_ATTRIBUTE_DIRECTORY & fileData.dwFileAttributes)
				continue;

			if (IsFileTimeNewer(&fileData.ftLastWriteTime, since))
			{
				swprintf_s(fileAndPath, L"%s\\%s;", (LPCWSTR)searchDir, fileData.cFileName);
				found += fileAndPath;
			}
		} while (FindNextFileW(hFile, &fileData));

		FindClose(hFile);
	}

	return found;
}

int FindFiles(CStringW searchDir, LPCWSTR spec, FileList& files, BOOL includeDirectories /*= FALSE*/,
              BOOL hideDotPrefixedItems /*= FALSE*/, BOOL recurse /*= FALSE*/)
{
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	CStringW searchSpec;
	CStringW fileAndPath;

	const WCHAR kLastChar = searchDir[searchDir.GetLength() - 1];
	if (kLastChar == L'\\' || kLastChar == L'/')
		searchDir = searchDir.Left(searchDir.GetLength() - 1);

	searchDir.Append(L"\\");
	searchSpec = searchDir + spec;
	hFile = FindFirstFileW(searchSpec, &fileData);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
				continue;

			const CStringW curFile(fileData.cFileName);
			if (hideDotPrefixedItems && curFile[0] == L'.')
			{
				// treat files and directories that start with '.' as hidden
				// do not descend into hidden directories
				continue;
			}

			const DWORD isDir = FILE_ATTRIBUTE_DIRECTORY & fileData.dwFileAttributes;
			if (isDir && !hideDotPrefixedItems && (curFile == L"." || curFile == L".."))
				continue;

			fileAndPath = searchDir + curFile;
			if (!isDir || includeDirectories)
				files.Add(fileAndPath);

			if (isDir && recurse)
				FindFiles(fileAndPath, spec, files, includeDirectories, hideDotPrefixedItems, recurse);
		} while (FindNextFileW(hFile, &fileData));

		FindClose(hFile);
	}

	return (int)files.size();
}

CStringW BuildTextDirList(CStringW searchDir, BOOL recurse, BOOL includeAllFiles, int depth)
{
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	CStringW result;
	const CStringW depthSpacing(L' ', depth * 2);
	std::wregex fileFilter;

	if (!includeAllFiles)
	{
		// [case: 141298]
		// ignore some files in db dirs to prevent unnecessary db rebuild
		std::wstring re(L"va.*[.]idx|va.*[.]tmp|sln[.]va|ff.*[.]va|.*[.]sdb");
		fileFilter = std::wregex(re, std::wregex::ECMAScript | std::wregex::optimize | std::wregex::icase);
	}

	const WCHAR kLastChar = searchDir[searchDir.GetLength() - 1];
	if (kLastChar == L'\\' || kLastChar == L'/')
		searchDir = searchDir.Left(searchDir.GetLength() - 1);

	searchDir.Append(L"\\");
	const CStringW searchSpec(searchDir + L"*.*");
	hFile = FindFirstFileW(searchSpec, &fileData);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		result += depthSpacing + searchDir + L"\r\n";
		CStringW tmp;
		do
		{
			if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
				continue;

			const CStringW curFile(fileData.cFileName);
			if (curFile == L"." || curFile == L"..")
				continue;

			if ((FILE_ATTRIBUTE_DIRECTORY & fileData.dwFileAttributes))
			{
				if (recurse)
				{
					tmp = searchDir + curFile;
					result += BuildTextDirList(tmp, true, includeAllFiles, depth + 1);
				}
				continue;
			}

			bool addCurFile = true;
			if (!includeAllFiles && std::regex_search(std::wstring(curFile), fileFilter))
				addCurFile = false;

			if (addCurFile)
			{
				CString__FormatW(tmp, L"%s  %s %ld %ld\r\n", (LPCWSTR)depthSpacing, (LPCWSTR)curFile,
				                 fileData.nFileSizeHigh, fileData.nFileSizeLow);
				result += tmp;
			}
		} while (FindNextFileW(hFile, &fileData));

		FindClose(hFile);
	}

	return result;
}

CStringW BuildTextDirList(CStringW searchDir, BOOL recurse /*= TRUE*/, BOOL includeAllFiles /*= TRUE*/)
{
	return BuildTextDirList(searchDir, recurse, includeAllFiles, 0);
}

static inline void RoundFileTime(LPFILETIME lpFT)
{
	// to handle case where VA is installed to a FAT volume
	//  and source files exist on NTFS volumes
	// FAT filetimes are rounded up to the nearest even second
	if (!lpFT)
		return;
	WORD date, time;
	// going in this direction will trash < 2 seconds of precision
	FileTimeToDosDateTime(lpFT, &date, &time);
	DosDateTimeToFileTime(date, time, lpFT);
}

BOOL WINAPI WTGetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime,
                          LPFILETIME lpLastWriteTime)
{
	if (!GetFileTime(hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime))
	{
		vLog("ERROR GetFileTime failed %08lx", GetLastError());
		return FALSE;
	}
	RoundFileTime(lpCreationTime);
	RoundFileTime(lpLastAccessTime);
	RoundFileTime(lpLastWriteTime);
	return TRUE;
}

void SetReadWrite(const CStringW& file)
{
	if (!ValidatePath(file))
		return;

	// confirm that it is a file and not a directory
	const DWORD attribs = GetFileAttributesW(file);
	if (INVALID_FILE_ATTRIBUTES == attribs || (attribs & FILE_ATTRIBUTE_DIRECTORY))
		return; // not a file
	if (attribs & FILE_ATTRIBUTE_READONLY)
		SetFileAttributesW(file, attribs & ~FILE_ATTRIBUTE_READONLY);
}

LPCWSTR
GetBaseName(LPCWSTR file)
{
	LPCWSTR f = file;
	for (LPCWSTR p = file; *p; p++)
		if (*p == L'\\' || *p == L'/')
			f = &p[1];
	return f;
}

LPCWSTR
GetBaseNameExt(LPCWSTR file)
{
	LPCWSTR f = file;
	for (LPCWSTR p = file; *p; p++)
	{
		switch (*p)
		{
		case L'.':
			f = &p[1];
			break;
		case L'\\':
		case L'/':
			f = file;
			break;
		}
	}
	return f;
}

CStringW GetBaseNameNoExt(LPCWSTR file)
{
	CStringW retval(file);
	const int pos1 = retval.ReverseFind(L'/');
	const int pos2 = retval.ReverseFind(L'\\');
	const int snipPos = max(pos1, pos2);
	if (snipPos != -1)
		retval = retval.Mid(snipPos + 1);
	int pos = retval.ReverseFind(L'.');
	if (-1 != pos)
		retval = retval.Left(pos);

	while (retval.Find(L'.') != -1)
	{
		// watch out for foo.cs.aspx / foo.aspx.cs / foo.designer.cs - remove all web extensions
		CStringW retvalLower(retval);
		retvalLower.MakeLower();
		int len = retval.GetLength(), tmp;
		pos = 0;

		tmp = retvalLower.Find(L".designer");
		if (!pos && tmp != -1)
			pos = tmp;

		tmp = retvalLower.Find(L".xaml");
		if (!pos && tmp != -1 && tmp == (len - 5))
			pos = tmp;

		tmp = retvalLower.Find(L".cs");
		if (!pos && tmp != -1 && tmp == (len - 3))
			pos = tmp;

		tmp = retvalLower.Find(L".vb");
		if (!pos && tmp != -1 && tmp == (len - 3))
			pos = tmp;

		tmp = retvalLower.Find(L".asp"); // .asp || .aspx
		if (!pos && tmp != -1)
			pos = tmp;

		if (!pos)
			break;

		pos = retval.ReverseFind(L'.');
		retval = retval.Left(pos);
	}

	return retval;
}

void MyRmDir(CStringW searchDir, bool recurse /*= false*/)
{
	if (!searchDir.GetLength()) // sanity check
		return;

	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	CStringW searchSpec;
	CStringW fileAndPath;

	const WCHAR kLastChar = searchDir[searchDir.GetLength() - 1];
	if (kLastChar == L'\\' || kLastChar == L'/')
		searchDir = searchDir.Left(searchDir.GetLength() - 1);

	if (searchDir.IsEmpty() || !IsDir(searchDir))
		return;

	searchDir.Append(L"\\");
	searchSpec = searchDir + L"*.*";
	hFile = FindFirstFileW(searchSpec, &fileData);
	if (hFile == INVALID_HANDLE_VALUE)
		return;

	do
	{
		if (INVALID_FILE_ATTRIBUTES == fileData.dwFileAttributes)
			continue;

		const DWORD isDir = FILE_ATTRIBUTE_DIRECTORY & fileData.dwFileAttributes;
		const CStringW curFile(fileData.cFileName);
		fileAndPath = searchDir + curFile;
		if (isDir)
		{
			if (recurse)
			{
				if (curFile != L"." && curFile != L"..")
					MyRmDir(fileAndPath, recurse);
			}
		}
		else
			DeleteFileW(fileAndPath);
	} while (FindNextFileW(hFile, &fileData));

	FindClose(hFile);
	_wrmdir(searchDir);
}

int RecycleFile(const CStringW& TargetFile)
{
	WCHAR deletePath[MAX_PATH + 1];
	int result = 0;

	ZeroMemory(deletePath, sizeof(WCHAR) * (MAX_PATH + 1));
	const DWORD status = GetFullPathNameW(TargetFile, MAX_PATH, deletePath, NULL);
	if (status && deletePath[0])
	{
		const DWORD fileAttr = GetFileAttributesW(deletePath);
		if (fileAttr != INVALID_FILE_ATTRIBUTES && (fileAttr & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			SHFILEOPSTRUCTW shFileOpInfo = {NULL};
			shFileOpInfo.wFunc = FO_DELETE;
			shFileOpInfo.pFrom = deletePath;
			shFileOpInfo.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

			SetReadWrite(CStringW(deletePath));
			result = SHFileOperationW(&shFileOpInfo);
		}
	}
	return result;
}

int GetTokenCount(const CStringW& str, WCHAR ch)
{
	int cnt = 0;
	for (int pos = str.Find(ch, 0); - 1 != pos; pos = str.Find(ch, pos + 1))
		++cnt;
	return cnt;
}

CStringW GetCompoundExt(const CStringW& f)
{
	CStringW ext;
	int pos = f.Find(L'.');
	if (-1 != pos)
		ext = f.Mid(pos + 1);
	return ext;
}

bool HasIgnoredFileExtension(const CStringW& filename)
{
	const CStringW list(Psettings ? L";" + CStringW(Psettings->mExtensionsToIgnore) : CStringW(L";"));
	if (list.GetLength() < 2)
		return false;

	const CStringW base(GetBaseName(filename));
	CStringW ext(L";." + CStringW(GetBaseNameExt(base)) + L";");
	if (list.Find(ext) != -1)
		return true;

	if (ext.GetLength() > 2)
	{
		// [case: 77993] check for compound extension
		int exts = GetTokenCount(base, L'.');
		if (exts > 1)
		{
			ext = L";." + CStringW(GetCompoundExt(base)) + L";";
			if (list.Find(ext) != -1)
				return true;
		}
	}

	return false;
}

CStringW GetTempFile(const CStringW& file, BOOL unicodeTarget /*= FALSE*/)
{
	CStringW asfile;
	CStringW fullFilePath = MSPath(file);
	fullFilePath
	    .MakeLower(); // bug 4590 - perforce checkout changes case of drive letter, so changing WTHashKeyW() value

	if (unicodeTarget)
		CString__FormatW(asfile, L"%s%s_LastMod_w_%s", (LPCWSTR)VaDirs::GetHistoryDir(),
		                 (LPCWSTR)utosw(WTHashKeyW(fullFilePath)), (LPCWSTR)Basename(fullFilePath));
	else
		CString__FormatW(asfile, L"%s%s_LastMod_%s", (LPCWSTR)VaDirs::GetHistoryDir(),
		                 (LPCWSTR)utosw(WTHashKeyW(fullFilePath)), (LPCWSTR)Basename(fullFilePath));
	asfile = MSPath(asfile);

	return asfile;
}

void RemoveTempFile(const CStringW& file)
{
	CStringW tmpFile;

	tmpFile = GetTempFile(file);
	if (IsFile(tmpFile))
	{
		::_wremove(tmpFile);
		_ASSERTE(!IsFile(tmpFile));
	}

	tmpFile = GetTempFile(file, TRUE);
	if (IsFile(tmpFile))
	{
		::_wremove(tmpFile);
		_ASSERTE(!IsFile(tmpFile));
	}
}

CStringW AutoSaveFile(CStringW file)
{
	//	_ASSERTE(Psettings->m_autoBackup);
	CStringW asfile;
	const CStringW fullFilePath = MSPath(file);
	const CStringW& kHistDir(VaDirs::GetHistoryDir());
	const CStringW kHash(utosw(WTHashKeyW(fullFilePath)));
	const CStringW kBase(Basename(fullFilePath));
	int i = 1;
	do
	{
		// use "vabak" prefix so that the purge in CheckAutoSaveHistory works properly
		CString__FormatW(asfile, L"%svabak_%s_%03d_%s", (LPCWSTR)kHistDir, (LPCWSTR)kHash, i++, (LPCWSTR)kBase);
	} while (IsFile(asfile) && !FilesAreIdentical(asfile, fullFilePath));

	if (IsFile(asfile))
		_wremove(asfile);
	return asfile;
}

void CheckAutoSaveHistory()
{
	// purge autosave history every n days
	WTString checkHistoryDate = (const char*)GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "HistoryCheck");
	if (checkHistoryDate.length())
	{
		int d, m, y;
		d = m = y = 0;
		sscanf(checkHistoryDate.c_str(), "%d.%d.%d", &y, &m, &d);
		try
		{
			if (CTime(y, m, d, 0, 0, 0) > CTime::GetCurrentTime())
				return;
		}
		catch (...)
		{
			VALOGEXCEPTION("CASH:");
			// bad args passed to CTime ctor
			// will happen if user sets system date to 3007 instead of 2007
		}
	}

	// save date of next check
	CTime nextChkTime = CTime::GetCurrentTime();
	nextChkTime += CTimeSpan(10, 0, 0, 0); // 10 days
	checkHistoryDate = nextChkTime.Format("%Y.%m.%d");
	SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, "HistoryCheck", checkHistoryDate.c_str());

	// AutoSaveFile uses the "vabak" prefix
	CleanDir(VaDirs::GetHistoryDir(), L"vabak_*.*");
}

bool IsRestrictedFileType(int ftype)
{
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
	// [case:147774] regardless to restrict setting, we need to allow .ini file in case of Unreal engine core redirect
	if (gIgnoreRestrictedFileType)
		return false;
#endif
	
	// keep in sync with use of mRestrictToPrimaryLangs in
	// CVATempDlg::InitTemplateTree (..\VATE\VATE\VATempDlg.cpp)
	switch (ftype)
	{
	case Src:
	case Header:
#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
	case UC:
	case CS:
	case XAML:
	case Idl:
	case RC:
	case Plain:
#endif
		return false;

	default:
#if defined(AVR_STUDIO) || defined(RAD_STUDIO)
		// ignore everything but .c, .h
		return true;
#else
		if (Psettings && Psettings->mRestrictVaToPrimaryFileTypes)
			return true;
#endif
	}

#if !defined(AVR_STUDIO) && !defined(RAD_STUDIO)
	_ASSERTE(!Psettings->mRestrictVaToPrimaryFileTypes);
	return false;
#endif
}

bool ShouldIgnoreFile(const CStringW& filename, bool tmpDirIsOk)
{
	if (sVaDir1.IsEmpty())
	{
		AutoLockCs l(sFileUtilLock);
		if (sVaDir1.IsEmpty())
		{
			CStringW tmpDir1(VaDirs::GetDllDir());
			tmpDir1.MakeLower();

			sVaDir2 = VaDirs::GetUserDir();
			sVaDir2.MakeLower();

			sVaDir3 = VaDirs::GetUserLocalDir();
			sVaDir3.MakeLower();

#if !defined(RAD_STUDIO)
			sVaDir5 = GetRegValueW(HKEY_LOCAL_MACHINE, ID_RK_APP, "InstPath");
			sVaDir5.MakeLower();
			if (-1 != tmpDir1.Find(sVaDir5) || -1 != sVaDir2.Find(sVaDir5))
			{
				// clear sVaDir5 if any overlap with the first 2 items
				sVaDir5.Empty();
			}
#endif // !RAD_STUDIO

			sTempDir1 = GetTempDir();
			if (!sTempDir1.IsEmpty())
			{
				sTempDir1.MakeLower();
				sTempDir2 = sTempDir1;
				sTempDir2.Replace(L"locals~1", L"local settings");
				vCatLog("Environment.Directories", "TempDir: %s\n", (LPCTSTR)CString(sTempDir1));
			}

			sVaDir1 = tmpDir1;
		}
	}

	CStringW lowerFile(filename);
	lowerFile.MakeLower();

	int ftype = GetFileTypeByExtension(lowerFile);
	if (IsRestrictedFileType(ftype))
		return true;

	if (-1 != lowerFile.Find(sVaDir1) || -1 != lowerFile.Find(sVaDir2) || -1 != lowerFile.Find(sVaDir3))
	{
		return true; // filter out VA install and user dirs
	}

	if (!sVaDir5.IsEmpty() && -1 != lowerFile.Find(sVaDir5))
		return true; // filter out VA install and user dirs

	if (gShellAttr->IsDevenv8OrHigher() && lowerFile.Find(L"~autorecover.") != -1)
	{
		return true; // filter out vs2005 auto recover
	}

	if (lowerFile.Find(L".svn") != -1 && lowerFile.Find(L".tmp.") != -1)
		return true; // subversion revision history
	if (-1 != lowerFile.Find(L"vsp") && -1 != lowerFile.Find(L".tmp."))
	{
		return true; // filter out VS power toys (?) vsp*.tmp.cs
	}

	if (tmpDirIsOk)
	{
		if (lowerFile.Find(sTempDir1) != -1 || lowerFile.Find(sTempDir2) != -1)
		{
			if (lowerFile.Find(L"\\nuget\\") != -1)
				return true; // nuget staging(?) dir
			if (lowerFile.Find(L"\\p4v\\") != -1)
				return true; // p4v revision history
			if (lowerFile.Find(L"\\p4win\\") != -1)
				return true; // p4win revision history
			if (lowerFile.Find(L"\\tfstemp\\") != -1)
				return true; // TFS revision history http://forums.wholetomato.com/forum/topic.asp?TOPIC_ID=11496
			if (lowerFile.Find(L"\\tfs\\") != -1)
				return true; // TFS revision history http://forums.wholetomato.com/forum/topic.asp?TOPIC_ID=11496
		}
	}
	else
	{
		if (lowerFile.Find(L".tlh") != -1)
			return false; // allow .tlh in temp dir
		if (lowerFile.Find(sTempDir1) != -1)
			return true;
		if (lowerFile.Find(sTempDir2) != -1)
			return true;
	}

	if (HasIgnoredFileExtension(lowerFile))
	{
		vLog("reject: ext %s", (LPCTSTR)CString(lowerFile));
		return true;
	}

	// case=9609 "mssql::"
	// search for "::", not sure what other formats might fall into the same category
	if (lowerFile.Find(L"::") != -1)
		return true;

	// case=27203 (and case=9609)
	if (lowerFile.Find(L"://") != -1 && lowerFile.Find(L"http://") == -1 && lowerFile.Find(L"file://") == -1)
		return true;

	// http://blogs.msdn.com/oldnewthing/archive/2008/09/19/8957958.aspx
	const CStringW pth(Path(lowerFile));
	if (pth.GetLength() != 3)
	{
		// #findref toptional 3%
		const DWORD dwAttr = GetFileAttributesW(pth);
		if (dwAttr != INVALID_FILE_ATTRIBUTES && ((dwAttr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) ==
		                                          (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
		{
			vLog("reject: %s %lx", (LPCTSTR)CString(lowerFile), dwAttr);
			return true;
		}
	}

	const CStringW base(Basename(lowerFile));
	if (base == L"xkeycheck.h")
	{
		// [case: 73347]
		return true;
	}

	return false;
}
using sif_key_t = std::pair<uint, bool>;
using sif_table_t = parallel_flat_hash_map_N<sif_key_t, bool, 4, std::shared_mutex>;
static sif_table_t sif_cache;
bool ShouldIgnoreFileCached(const DefObj& sym, bool tmpDirIsOk)
{
	sif_key_t key = std::make_pair(sym.FileId(), tmpDirIsOk);
	bool ret = false;
	if (!sif_cache.if_contains(key, [&ret](sif_table_t::value_type& v) {
		auto& [_key, _value] = v;
		ret = _value;
	}))
	{
		ret = ShouldIgnoreFile(sym.FilePath(), tmpDirIsOk);
		sif_cache.lazy_emplace_l(
		    key,
		    [](sif_table_t::value_type& v) {},
		    [&key, ret](const sif_table_t::constructor& ctor) {
			    ctor(key, ret);
		    });
	}
	assert(ret == ShouldIgnoreFile(sym.FilePath(), tmpDirIsOk));
	return ret;
}
void ClearShouldIgnoreFileCache()
{
	sif_cache.clear();
}

bool ShouldFileBeAttachedTo(const CStringW& filename, bool checkSize /*= true*/)
{
	if (!Psettings || !Psettings->m_validLicense)
		return false;

	if (checkSize && GetFSize(filename) > Psettings->mLargeFileSizeThreshold)
	{
		// not active on files greater than 5 meg
		vLog("Attach: reject large file %s\n", (LPCTSTR)CString(filename));
		return false;
	}

	if (ShouldIgnoreFile(filename, true))
	{
		vLog("Attach: reject %s\n", (LPCTSTR)CString(filename));
		return false;
	}

	if (SQL == GetFileType(filename))
		return false;

	return true;
}

bool IsLikelyXmlFile(const CStringW& filename)
{
	CStringW ext(GetBaseNameExt(filename));
	ext.MakeLower();
	if (ext == L"xml")
		return true;

	if (-1 == filename.Find(L"\\\\"))
	{
		WTString contents;
		if (!contents.ReadFile(filename, 10))
			return false;

		if (contents.GetLength() < 5)
			return false;

		if (!_tcsnicmp("<?xml", contents.c_str(), 5))
			return true;
	}

	return false;
}

CStringW GetFileByType(const CStringW& file, int type)
{
	CStringW fname(file);
	if (GetFileType(fname) == type)
		return fname;

	FileList possibleMatches;
	GetBestFileMatch(fname, possibleMatches);
	if (1 == possibleMatches.size())
	{
		const CStringW retval(possibleMatches.begin()->mFilename);
		if (GetFileType(retval) == type)
			return retval;
	}

	if (SwapExtension(fname) && fname.GetLength())
	{
		if (GetFileType(fname) == type)
			return fname;
	}

	// [case: 61837] added due to xaml/cpp confusion when looking for header
	int lastChanceMatches = 0;
	fname.Empty();
	CStringW base(GetBaseNameNoExt(file));
	base.MakeLower();
	for (FileList::const_iterator it = possibleMatches.begin(); it != possibleMatches.end(); ++it)
	{
		const CStringW cur = (*it).mFilenameLower;
		const int fType = GetFileType(cur);
		if (fType == type && base == GetBaseNameNoExt(cur))
		{
			++lastChanceMatches;
			if (fname.IsEmpty())
				fname = cur;
		}
	}

	if (lastChanceMatches > 1)
	{
		vLog("WARN: GetFileByType: %s returning %s with %d other hits\n", (LPCTSTR)CString(file),
		     (LPCTSTR)CString(fname), lastChanceMatches - 1);
	}

	return fname;
}

CStringW NormalizeFilepath(LPCWSTR file)
{
	CStringW filename(file);
	if (filename.GetLength() > 1 && filename[1] == L':')
	{
		CStringW tmp(MSPath(filename));
		if (tmp != filename)
		{
			vLog("Normalize: %s -> %s\n", (LPCTSTR)CString(filename), (LPCTSTR)CString(tmp));
			filename = tmp;
		}
	}
	return filename;
}

CStringW BuildRelativePath(CStringW toFile, CStringW relativeToDir)
{
	_ASSERTE(!toFile.IsEmpty());
	if (toFile.IsEmpty())
		return toFile;

	CStringW dirOfFileLower(Path(toFile));
	dirOfFileLower.MakeLower();
	dirOfFileLower.Replace(L'/', L'\\');
	if (dirOfFileLower[dirOfFileLower.GetLength() - 1] != L'\\')
		dirOfFileLower += L'\\';
	CStringW relativeToDirLower(relativeToDir);
	relativeToDirLower.MakeLower();
	relativeToDirLower.Replace(L'/', L'\\');
	if (relativeToDirLower[relativeToDirLower.GetLength() - 1] != L'\\')
		relativeToDirLower += L'\\';

	if (dirOfFileLower.Find(relativeToDirLower) == 0)
	{
		// toFile is beneath relativeToDir - remove relativeToDir from toFile
		const int relLen = relativeToDirLower.GetLength();
		toFile = toFile.Mid(relLen);
		return toFile;
	}

	if (relativeToDir.GetLength() > 1 && relativeToDir[1] == L':')
		relativeToDir.SetAt(0, relativeToDirLower[0]);
	if (toFile.GetLength() > 1 && toFile[1] == L':')
		toFile.SetAt(0, dirOfFileLower[0]);
	bool hasCommonPath = false;
	for (;;)
	{
		const int slashPos1 = relativeToDir.FindOneOf(L"/\\");
		const int slashPos2 = toFile.FindOneOf(L"/\\");
		if (!hasCommonPath)
		{
			if (slashPos1 == -1 || slashPos2 == -1)
				break;
			if (slashPos1 != slashPos2)
				break;
		}

		if (slashPos1 != -1 && slashPos2 != -1)
		{
			CStringW tmp1, tmp2;
			if (slashPos1 != -1)
				tmp1 = relativeToDir.Left(slashPos1);
			if (slashPos2 != -1)
				tmp2 = toFile.Left(slashPos2);

			if (!tmp1.CompareNoCase(tmp2) && !tmp1.IsEmpty())
			{
				hasCommonPath = true;
				relativeToDir = relativeToDir.Mid(slashPos1 + 1);
				toFile = toFile.Mid(slashPos2 + 1);
				continue;
			}
		}

		break;
	}

	if (hasCommonPath)
	{
		CStringW res;
		// there was a common path and now we are down to two distinct path fragments.
		// for each dir left in relativeToDir, add ../
		for (;;)
		{
			res += L"..\\";
			int slashPos = relativeToDir.FindOneOf(L"/\\");
			if (-1 == slashPos || slashPos == relativeToDir.GetLength() - 1)
				break;
			relativeToDir = relativeToDir.Mid(slashPos + 1);
		}

		// append what's left of toFile
		res += toFile;
		return res;
	}

	return toFile;
}

bool BuildRelativePathEx(CStringW& result, CStringW toFile, CStringW relativeToDir)
{
	CStringW dirOfFileLower(Path(toFile));
	dirOfFileLower.MakeLower();
	dirOfFileLower.Replace(L'/', L'\\');
	if (dirOfFileLower[dirOfFileLower.GetLength() - 1] != L'\\')
		dirOfFileLower += L'\\';
	CStringW relativeToDirLower(relativeToDir);
	relativeToDirLower.MakeLower();
	relativeToDirLower.Replace(L'/', L'\\');
	if (relativeToDirLower[relativeToDirLower.GetLength() - 1] != L'\\')
		relativeToDirLower += L'\\';

	if (dirOfFileLower.Find(relativeToDirLower) == 0)
	{
		// toFile is beneath relativeToDir - remove relativeToDir from toFile
		const int relLen = relativeToDirLower.GetLength();
		toFile = toFile.Mid(relLen);
		result = L".\\" + toFile;
		return true;
	}

	if (relativeToDir.GetLength() > 1 && relativeToDir[1] == L':')
		relativeToDir.SetAt(0, relativeToDirLower[0]);
	if (toFile.GetLength() > 1 && toFile[1] == L':')
		toFile.SetAt(0, dirOfFileLower[0]);
	bool hasCommonPath = false;
	for (;;)
	{
		const int slashPos1 = relativeToDir.FindOneOf(L"/\\");
		const int slashPos2 = toFile.FindOneOf(L"/\\");
		if (!hasCommonPath)
		{
			if (slashPos1 == -1 || slashPos2 == -1)
				break;
			if (slashPos1 != slashPos2)
				break;
		}

		if (slashPos1 != -1 && slashPos2 != -1)
		{
			CStringW tmp1, tmp2;
			if (slashPos1 != -1)
				tmp1 = relativeToDir.Left(slashPos1);
			if (slashPos2 != -1)
				tmp2 = toFile.Left(slashPos2);

			if (!tmp1.CompareNoCase(tmp2) && !tmp1.IsEmpty())
			{
				hasCommonPath = true;
				relativeToDir = relativeToDir.Mid(slashPos1 + 1);
				toFile = toFile.Mid(slashPos2 + 1);
				continue;
			}
		}

		break;
	}

	if (hasCommonPath)
	{
		// there was a common path and now we are down to two distinct path fragments.
		// for each dir left in relativeToDir, add ../
		for (;;)
		{
			result += L"..\\";
			int slashPos = relativeToDir.FindOneOf(L"/\\");
			if (-1 == slashPos || slashPos == relativeToDir.GetLength() - 1)
				break;
			relativeToDir = relativeToDir.Mid(slashPos + 1);
		}

		// append what's left of toFile
		result += toFile;
		return true;
	}

	return false;
}

int ContainsVersionString(const CStringW& str)
{
	try
	{
		for (int i = 0; i < str.GetLength(); i++)
		{
			if (isdigit(str[i]))
			{
				for (int j = i + 1; j < str.GetLength(); j++)
				{
					if (isdigit(str[j]))
						continue;

					if (str[j] == '.')
						return true;
				}
			}
		}
	}
	catch (...)
	{
		_ASSERTE(!"Exception caught in ContainsVersionString!");
	}

	return false;
}

// makes string more compatible by replacing portions of version string
// so if you have path like   "c:\SomeSystemFramework\10.2.1565.5\lib"
// you may get something like "c:\SomeSystemFramework\10.2.0000.0\lib"
// start_part - specifies where digits replacing with 0 starts,
//				0 = major, 1 = minor, 2 = build, 3 = revision/private,
//				set start_part to 2 to preserve major and minor parts
CStringW NormalizeVersionStrings(const CStringW& str_in, int start_part = 2) 
{
	CStringW str = str_in;

	try
	{
		for (int i = 0; i < str.GetLength(); i++)
		{
			if (isdigit(str[i]))
			{
				bool isVersion = false;

				for (int j = i + 1; j < str.GetLength(); j++)
				{
					if (isdigit(str[j]))
						continue;

					if (str[j] == '.')
						isVersion = true;

					break;
				}

				if (isVersion)
				{
					int dots = 0;
					for (; i < str.GetLength(); i++)
					{
						if (isdigit(str[i]))
						{
							if (dots >= start_part)
								str.SetAt(i, L'0');

							continue;
						}

						if (str[i] == '.')
						{
							dots++;
							continue;
						}

						break;
					}
				}
			}
		}
	}
	catch (...)
	{
		_ASSERTE(!"Exception caught in NormalizeVersionStrings!");
		str.Empty();
	}

	return str;
};

CStringW BuildIncludeDirective(CStringW headerFileToInclude, CStringW activeFile, WCHAR pathDelimiter,
                               DWORD addIncludePath, BOOL sysOverride /*= FALSE*/)
{
	_ASSERTE(GlobalProject && GlobalProject->IsOkToIterateFiles());
	BOOL isSys = IncludeDirs::IsSystemFile(headerFileToInclude);
	bool overrideSysToken = false; // if true, excludes isSys from being considered for use of <> [case: 111997]

	CStringW shortestPathToHeader(headerFileToInclude);
	if ((gShellAttr && gShellAttr->IsCppBuilder() && isSys) || 3 == Psettings->mAddIncludeStyle || 4 == Psettings->mAddIncludeStyle || 5 == Psettings->mAddIncludeStyle)
	{
		shortestPathToHeader = Basename(headerFileToInclude);
	}
	else
	{
		if (Path(headerFileToInclude) == Path(activeFile) && addIncludePath != tagSettings::PP_RELATIVE_TO_PROJECT)
		{
			shortestPathToHeader = Basename(headerFileToInclude);
			isSys = false;
			overrideSysToken = true;
		}
		else
		{
			bool doSys = false;

			for (;;)
			{
				CStringW tmp;
				FileList dirs;
				int projectsInside = 0;
				CStringW projRelPath;
				CStringW projectPathUpper;
				int fileInProjects = 0;

				if (doSys)
				{
					IncludeDirs sysDirs;
					TokenW incDirs = sysDirs.getSysIncludes();
					while (incDirs.more())
					{
						tmp = incDirs.read(L";");
						if (tmp.GetLength())
							dirs.Add(tmp);
					}

					tmp.Empty();
				}
				else
				{
					CStringW matchedActiveFile;
					matchedActiveFile = activeFile;
					SwapExtension(matchedActiveFile);
					if (matchedActiveFile == activeFile)
						matchedActiveFile.Empty();

					RWLockReader lck;
					const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
					// iterate over projects - add inc dirs only for projects that contain activeFile
					for (Project::ProjectMap::const_iterator projIt = projMap.begin(); projIt != projMap.end();
					     ++projIt)
					{
						const ProjectInfoPtr projInf = (*projIt).second;
						bool projContainsActiveFile = projInf && projInf->ContainsFile(activeFile);
						if (projInf && (projContainsActiveFile ||
						                (matchedActiveFile.GetLength() && projInf->ContainsFile(matchedActiveFile))))
						{
							if (!isSys)
							{
								if (projContainsActiveFile && addIncludePath == tagSettings::PP_RELATIVE_TO_PROJECT)
								{
									fileInProjects++;
									const CStringW& projectFile = projInf->GetProjectFile();
									CStringW projectPath = Path(projectFile);
									if (headerFileToInclude.GetLength() >= projectPath.GetLength())
									{
										CStringW headerPathLeft = headerFileToInclude.Left(projectPath.GetLength());
										CStringW headerPathLeftUpper = headerPathLeft;
										headerPathLeftUpper.MakeUpper();
										projectPathUpper = projectPath;
										projectPathUpper.MakeUpper();
										if (projectPathUpper == headerPathLeftUpper)
										{
											CStringW old = projRelPath;
											projRelPath = BuildRelativePath(headerFileToInclude, projectPath);
											if (old !=
											    projRelPath) // if two projects has the same path, we don't count those
											                 // as different ones. AST AddIncludeProjRel10 is an example
											                 // where the 2 projects are counted here
												projectsInside++;
										}
										else
										{
											return BuildIncludeDirective(
											    headerFileToInclude, activeFile, pathDelimiter,
											    tagSettings::PP_SHORTEST_POSSIBLE,
											    sysOverride); // fall-back. see AST AddIncludeProjRel06
										}
									}
									else
									{
										return BuildIncludeDirective(headerFileToInclude, activeFile, pathDelimiter,
										                             tagSettings::PP_SHORTEST_POSSIBLE,
										                             sysOverride); // fall-back
									}
								}
							}

							dirs.Add(projInf->GetIncludeDirs());
							dirs.Add(projInf->GetPlatformIncludeDirs());

							if ((gShellAttr && gShellAttr->IsDevenv17OrHigher()) ||
							    (Psettings && Psettings->mForceExternalIncludeDirectories))
							{
								// [case: 145995]
								dirs.Add(projInf->GetPlatformExternalIncludeDirs());
							}

							// [case: 150119] fix for Unreal Engine missing InstallPath
							std::pair<int, int> ueVersionNumber = GlobalProject->GetUnrealEngineVersionAsNumber();
							if (ueVersionNumber.first > 5 || ueVersionNumber.first == 5 && ueVersionNumber.second >= 3) // >= 5.3
							{
								for (const auto& ueIncPath : GlobalProject->GetUnrealIncludePathList())
								{
									dirs.AddUniqueNoCase(ueIncPath);
								}
							}
						}
					}
				}

				if (addIncludePath == tagSettings::PP_RELATIVE_TO_PROJECT)
				{
					if (projectsInside == 1)
					{
						shortestPathToHeader = projRelPath;
						CStringW curDir;
						int files = 0;

						// check if the project's dir can be found among the include dirs
						for (FileList::const_iterator it = dirs.begin(); it != dirs.end(); ++it)
						{
							curDir = (*it).mFilename;
							curDir.MakeUpper();
							if (curDir == projectPathUpper)
								files++;
						}
						if (files == fileInProjects) // all projects must have themselves on the list of include
						                             // directories in project properties
						{
							_ASSERTE(!doSys);
							break; // safe to break - doSys is false (since projectsInside is only increased when it is
							       // false) so no need to iterate the "for (;;)"
						}
						return BuildIncludeDirective(headerFileToInclude, activeFile, pathDelimiter,
						                             tagSettings::PP_SHORTEST_POSSIBLE,
						                             sysOverride); // fall-back if project's path is NOT added to the
						                                           // project's additional include directories
					}
					else
					{
						return BuildIncludeDirective(
						    headerFileToInclude, activeFile, pathDelimiter, tagSettings::PP_SHORTEST_POSSIBLE,
						    sysOverride); // fall-back. AST AddIncludeProjRel10 triggers this path
					}
				}

				CStringW hfNorm;
				if (isSys && ContainsVersionString(headerFileToInclude))
					hfNorm = NormalizeVersionStrings(headerFileToInclude);

				// iterate over dirs - keep shortest result
				bool keepIt; // [case: 62702]
				for (FileList::const_iterator it = dirs.begin(); it != dirs.end(); ++it)
				{
					const CStringW & curDir = (*it).mFilename;
					if (!curDir.GetLength())
						continue;

					int repeat_count = !hfNorm.IsEmpty() && ContainsVersionString(curDir) ? 2 : 1;
					for (int repeat = 0; repeat < repeat_count; repeat++)
					{
						keepIt = false;
						if (!repeat)
							tmp = BuildRelativePath(headerFileToInclude, curDir);
						else
						{
							if (!hfNorm.IsEmpty())
								tmp = BuildRelativePath(hfNorm, NormalizeVersionStrings(curDir));
							else
								break;
						}
							     					    
						if (shortestPathToHeader.IsEmpty())
							keepIt = true;
						else if (tmp[1] != L':' && shortestPathToHeader[1] == L':')
							keepIt = true;
						else if (tmp.GetLength() < shortestPathToHeader.GetLength())
						{
							if (tmp[0] != L'.' || (tmp[0] == L'.' && addIncludePath == tagSettings::PP_SHORTEST_POSSIBLE))
								keepIt = true;
						}

						if (addIncludePath != tagSettings::PP_SHORTEST_POSSIBLE && tmp[0] != L'.' &&
						    shortestPathToHeader[0] == L'.')
							keepIt = true;

						if (keepIt && (!repeat || !ContainsVersionString(tmp)))
						{
							shortestPathToHeader = tmp;
							overrideSysToken = false;
						}
					}
				}

				// repeat against active file
				keepIt = false;
				tmp = BuildRelativePath(headerFileToInclude, Path(activeFile));
				if (shortestPathToHeader.IsEmpty())
					keepIt = true;
				else if (tmp[1] != L':' && shortestPathToHeader[1] == L':')
					keepIt = true;
				else if (tmp.GetLength() < shortestPathToHeader.GetLength())
				{
					if (tmp[0] != L'.' || (tmp[0] == L'.' && addIncludePath == tagSettings::PP_SHORTEST_POSSIBLE))
						keepIt = true;
					else if (!shortestPathToHeader.IsEmpty() && shortestPathToHeader[0] == L'.' &&
					         addIncludePath == tagSettings::PP_RELATIVE_TO_FILE_OR_INCLUDE_DIR)
						keepIt = true; // [case: 119677] use the shortest path that backtracks if that's all that can be
						               // found
				}

				if (addIncludePath != tagSettings::PP_SHORTEST_POSSIBLE && tmp[0] != L'.' &&
				    shortestPathToHeader[0] == L'.')
					keepIt = true;

				if (keepIt)
				{
					shortestPathToHeader = tmp;
					overrideSysToken = true;
				}

				if ((shortestPathToHeader == headerFileToInclude) ||
				    (shortestPathToHeader.GetLength() > headerFileToInclude.GetLength()))
				{
					if (doSys)
						break;
					else if (isSys)
						doSys = true;
					else
						break;
				}
				else
				{
					if (keepIt)
						isSys = false;

					break;
				}
			}
		}
	}

	const bool doesPathBacktrack = shortestPathToHeader.GetLength() > 2 && shortestPathToHeader[0] == '.' &&
	                               shortestPathToHeader[1] == '.' &&
	                               (shortestPathToHeader[2] == '\\' || shortestPathToHeader[2] == '/');

	if (doesPathBacktrack)
	{
		CStringW shortestPathUniform = shortestPathToHeader;
		shortestPathUniform.Replace(L'/', L'\\');
		shortestPathUniform.MakeLower();
		const auto pathSize = MAX_PATH + 1;
		WCHAR progFilesName[pathSize] = L"";
		WCHAR progFilesNameX86[pathSize] = L"";
		_wsplitpath_s(kWinProgFilesDir, nullptr, 0, nullptr, 0, progFilesName, pathSize, nullptr, 0);
		_wsplitpath_s(kWinProgFilesDirX86, nullptr, 0, nullptr, 0, progFilesNameX86, pathSize, nullptr, 0);
		CStringW progFilesFragUniform;
		CStringW progFilesFragUniformX86;
		CString__AppendFormatW(progFilesFragUniform, L"\\%s\\", progFilesName);
		CString__AppendFormatW(progFilesFragUniformX86, L"\\%s\\", progFilesNameX86);
		progFilesFragUniform.MakeLower();
		progFilesFragUniformX86.MakeLower();

		const bool isProgFilesFound = shortestPathUniform.Find(progFilesFragUniform) != -1 ||
		                              shortestPathUniform.Find(progFilesFragUniformX86) != -1;

		if (isProgFilesFound)
		{
			CStringW headerPathUniform = headerFileToInclude;
			CStringW progFilesDirUniform = kWinProgFilesDir;
			CStringW progFilesDirUniformX86 = kWinProgFilesDirX86;
			headerPathUniform.Replace(L'/', L'\\');
			headerPathUniform.MakeLower();
			progFilesDirUniform.Replace(L'/', L'\\');
			progFilesDirUniform.MakeLower();
			progFilesDirUniformX86.Replace(L'/', L'\\');
			progFilesDirUniformX86.MakeLower();

			const bool isTrueProgFiles =
			    headerPathUniform.Find(progFilesDirUniform) == 0 || headerPathUniform.Find(progFilesDirUniformX86) == 0;

			if (isTrueProgFiles)
			{
				// [case: 112239] never produce a relative path directive that goes to root and back out to prog files
				shortestPathToHeader = headerFileToInclude;
				// prefer <> tokens
				isSys = true;
				overrideSysToken = false;
			}
		}
	}

	if (shortestPathToHeader == headerFileToInclude)
		shortestPathToHeader = Basename(headerFileToInclude);

	if (sysOverride)
	{
		// [case: 36075] hacky workaround for multiple platforms.  this fixes
		// add include of std::string.  In vs2012, when both vs2010 and vs2012
		// projects have been opened, sometimes add include of string yields
		// "string" instead of <string> due to a hit in the vc10 include dir
		// that is not an include dir when a vs2012 project is active.
		isSys = sysOverride;
	}

	if (shortestPathToHeader.Find(L"..\\..\\..") != -1 || shortestPathToHeader.Find(L"../../..") != -1)
	{
		CStringW lowerGenPath = shortestPathToHeader;
		lowerGenPath.MakeLower();

		if (lowerGenPath.Find(L"\\vc\\") != -1 || lowerGenPath.Find(L"/vc/") != -1)
		{
			CStringW lowerHeaderPath = headerFileToInclude;
			CStringW lowerProgFilesDir = kWinProgFilesDir;
			CStringW lowerProgFilesDirx86 = kWinProgFilesDirX86;
			lowerHeaderPath.MakeLower();
			lowerProgFilesDir.MakeLower();
			lowerProgFilesDirx86.MakeLower();

			if (lowerHeaderPath.Find(lowerProgFilesDir) != -1 || lowerHeaderPath.Find(lowerProgFilesDirx86) != -1)
			{
				// prevent incorrect symbol include paths which can result from mismatched std/crt/atl/mfc/etc. include
				// directories [case: 112112]
				shortestPathToHeader = Basename(headerFileToInclude);
				// prefer <> tokens
				isSys = true;
				overrideSysToken = false;
			}
		}
	}

	const bool useAngle =
	    ((isSys && !overrideSysToken) || 2 == Psettings->mAddIncludeStyle || 5 == Psettings->mAddIncludeStyle) &&
	    Psettings->mAddIncludeStyle != 1 && Psettings->mAddIncludeStyle != 4;

	// [case:149198] check unreal override option to always use quotation marks for Add Include
	bool unrealAlwaysUseQuotation = Psettings->mUnrealEngineCppSupport && Psettings->mAddIncludeUnrealUseQuotation;
	
	CStringW directive;
	if (useAngle && !unrealAlwaysUseQuotation)
		CString__FormatW(directive, L"#include <%s>", (LPCWSTR)shortestPathToHeader);
	else
		CString__FormatW(directive, L"#include \"%s\"", (LPCWSTR)shortestPathToHeader);

	if (pathDelimiter && pathDelimiter != L'\\' && directive.Find(L'\\') != -1)
		directive.Replace(L'\\', pathDelimiter);

	return directive;
}

// cache results to prevent repetitive calls to slow GetFileByType function
static ThreadStatic<CStringW> sIsCfileLastFile;
static ThreadStatic<bool> sIsCfileResultForLastFile;

bool IsCfile(const CStringW& fName)
{
	if (sIsCfileLastFile() == fName)
		return sIsCfileResultForLastFile();

	sIsCfileLastFile() = fName;
	CStringW ext(GetBaseNameExt(fName));
	if (ext == L"c")
	{
		sIsCfileResultForLastFile() = true;
		return true;
	}

	const int fType = GetFileType(fName);
	if (Header == fType)
	{
		// see if we can locate the src and id it's type
		CStringW srcFile(fName);
		const CStringW tmp(GetFileByType(srcFile, Src));
		if (tmp.GetLength())
		{
			ext = GetBaseNameExt(srcFile);
			if (ext == L"c")
			{
				sIsCfileResultForLastFile() = true;
				return true;
			}
		}
	}

	sIsCfileResultForLastFile() = false;
	return false;
}

// Does path fragment contain only '.' and '\\' and '/'?
// Note, if fragment begins with '\' or '\\' then it is not
// considered pure relative.
bool IsPathFragmentPureRelative(const CStringW& frag)
{
	const int kLen = frag.GetLength();
	for (int idx = 0; idx < kLen; ++idx)
	{
		switch (frag[idx])
		{
		case L'.':
			break;
		case L'\\':
		case L'/':
			if (idx == 0)
				return false;
			break;
		default:
			return false;
		}
	}
	return true;
}

CStringW BuildPath(const CStringW& fragment, const CStringW& relativeToDir,
                   bool forFile /*= true*/,   // file or directory
                   bool mustExist /*= true*/) // will return empty string if non-existent and mustExist is true
{
	CStringW builtPath(fragment);
	bool tryProjPathDrive = false;

	if (builtPath.Find(L'\"') != -1)
		builtPath.Replace(L"\"", L"");

	if (builtPath.Find(L'$') != -1 || builtPath.Find(L'%') != -1)
		builtPath = WTExpandEnvironmentStrings(builtPath);

	bool attemptPathMunge = true;
	if ((forFile && IsFile(builtPath)) || (!forFile && IsDir(builtPath)))
	{
		if (!IsPathFragmentPureRelative(builtPath))
			attemptPathMunge = false;
	}

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		const int dirLen = relativeToDir.GetLength();
		const bool dirEndsWithSlash = dirLen && (relativeToDir[dirLen - 1] == '\\' || relativeToDir[dirLen - 1] == '/');

		if (attemptPathMunge)
		{
			if (builtPath.GetLength() > 2 && builtPath[1] == L':')
			{
				// fullpath c:/...
			}
			else if (forFile && !dirEndsWithSlash && IsFile(relativeToDir + L"\\" + builtPath))
			{
				builtPath = relativeToDir + L"\\" + builtPath;
			}
			else if (forFile && dirEndsWithSlash && IsFile(relativeToDir + builtPath))
			{
				builtPath = relativeToDir + builtPath;
			}
			else if (!forFile && !dirEndsWithSlash && IsDir(relativeToDir + L"\\" + builtPath))
			{
				builtPath = relativeToDir + L"\\" + builtPath;
			}
			else if (!forFile && dirEndsWithSlash && IsDir(relativeToDir + builtPath))
			{
				builtPath = relativeToDir + builtPath;
			}
			else if (::IsPathRootAbsolute(builtPath))
			{
				tryProjPathDrive = true;
			}
			else if (builtPath.Find(L"${") == -1 && builtPath.Find(L"$(") == -1 && builtPath.Find(L'%') == -1)
			{
				// relative path, prepend project dir
				if (dirEndsWithSlash)
					builtPath = relativeToDir + builtPath;
				else
					builtPath = relativeToDir + L"\\" + builtPath;
			}
		}
		else
		{
			tryProjPathDrive = true;
		}

		if (tryProjPathDrive && ::IsPathRootAbsolute(builtPath))
		{
			// absolute path without drive specification, prepend project drive.
			// can't rely on MSPath or _fullpath since working directory might be
			// on a different drive than the project is on.
			// This is not appropriate when resolving headers - but is
			// appropriate when resolving directories based on paths
			// in the project files. (case=2512)
			const CStringW projectDrive = relativeToDir.Mid(0, relativeToDir.FindOneOf(L"/\\"));
			if (2 == projectDrive.GetLength()) // c:
				builtPath = projectDrive + builtPath;
		}
		else if (!attemptPathMunge && builtPath[0] == L'.' && relativeToDir.GetLength())
		{
			if (dirEndsWithSlash)
				builtPath = relativeToDir + builtPath;
			else
				builtPath = relativeToDir + L'\\' + builtPath;
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("BP:");
		Log("Exception caught in BuildPath");
		ASSERT(FALSE);
		builtPath.Empty();
		return builtPath;
	}
#endif // !SEAN

	if (builtPath.Find(L"${") == -1 && builtPath.Find(L"$(") == -1 && builtPath.Find(L'%') == -1)
	{
		if (forFile)
		{
			// MSPath will add full path
			builtPath = MSPath(builtPath);
			if (mustExist && !IsFile(builtPath))
				builtPath.Empty();
		}
		else // for directory
		{
			const int kMaxPathLength = 512;
			const std::unique_ptr<WCHAR[]> bufVec(new WCHAR[kMaxPathLength + 1]);
			WCHAR* full = &bufVec[0];
			if (_wfullpath(full, builtPath, kMaxPathLength))
				builtPath = full;
			if (mustExist && !IsDir(builtPath))
				builtPath.Empty();
			const int len = builtPath.GetLength();
			// changed len compare to 3 because GetFullPathName in VerifyPathList
			// will change c: into c:\the\current\directory\on\c ( if builtPath here is c:\ )
			if (len > 3 && (builtPath[len - 1] == L'\\' || builtPath[len - 1] == L'/'))
				builtPath = builtPath.Left(len - 1);
		}
	}
	else if (mustExist)
		builtPath.Empty();

	return builtPath;
}

LPCTSTR
GetFileType(int ftype)
{
	switch (ftype)
	{
	case Plain:
		return _T("Plain");
	case Java:
		return _T("Java");
	case Tmp:
		return _T("Tmp");
	case RC:
		return _T("RC");
	case Other:
		return _T("Other");
	case Src:
		return _T("Src");
	case Header:
		return _T("Header");
	case Binary:
		return _T("Binary");
	case Idl:
		return _T("Idl");
	case HTML:
		return _T("HTML");
	case VB:
		return _T("VB");
	case PERL:
		return _T("PERL");
	case JS:
		return _T("JS");
	case CS:
		return _T("CS");
	case SQL:
		return _T("SQL");
	case Image:
		return _T("Image");
	case UC:
		return _T("UC");
	case PHP:
		return _T("PHP");
	case ASP:
		return _T("ASP");
	case XAML:
		return _T("XAML");
	case XML:
		return _T("XML");
	case VBS:
		return _T("VBS");
	default:
		_ASSERTE(!"unhandled file type in GetFileType (return string)");
		return _T("Unknown file type");
	}
}

CStringW GetTempDir()
{
	if (!sTempDir0.IsEmpty())
		return sTempDir0;

	AutoLockCs l(sFileUtilLock);
	if (sTempDir0.IsEmpty())
	{
		WCHAR buf[MAX_PATH + 1];
		if (GetTempPathW(MAX_PATH, buf) > 0)
		{
			buf[MAX_PATH] = L'\0';
			sTempDir0 = buf;
		}
	}

	return sTempDir0;
}

// [case: 141741]
bool IsUEIgnorePluginsDir(const CStringW& filename)
{
	static std::vector<CStringW> pathListCache;

	CStringW pth(Path(filename));
	pth.MakeLower();

	if (pathListCache.empty())
	{
		for (const auto& uePathRoot : GlobalProject->GetUEIgnorePluginsDirs())
		{
			for (const auto& uePath : uePathRoot.second)
			{
				if (pth.GetLength() >= uePath.GetLength())
				{
					if (pth.Find(uePath) > -1)
					{
						pathListCache = uePathRoot.second;
						if (Psettings->mIndexPlugins == 1) // index only referenced plugins
						{
							for (const auto& referencedPluginPath : GlobalProject->GetReferencedUEPlugins())
							{
								if (pth.Find(referencedPluginPath) > -1)
									return false; // do not ignore plugin since it is in referenced list
							}
						}
						return true;
					}
				}
			}
		}
	}
	else
	{
		for (const auto& uePath : pathListCache)
		{
			if (pth.Find(uePath) > -1)
			{
				if (Psettings->mIndexPlugins == 1) // index only referenced plugins
				{
					for (const auto& referencedPluginPath : GlobalProject->GetReferencedUEPlugins())
					{
						if (pth.Find(referencedPluginPath) > -1)
							return false; // do not ignore plugin since it is in referenced list
					}
				}
				return true;
			}
		}
	}

	return false;
}

// From Path.GetInvalidFileNameChars()
static const wchar_t InvalidFileNameChars[41] = {
    '"',      '<',      '>',      '|',      '\x0001', '\x0002', '\x0003', '\x0004', '\x0005', '\x0006', '\a',
    '\b',     '\t',     '\n',     '\v',     '\f',     '\r',     '\x000E', '\x000F', '\x0010', '\x0011', '\x0012',
    '\x0013', '\x0014', '\x0015', '\x0016', '\x0017', '\x0018', '\x0019', '\x001A', '\x001B', '\x001C', '\x001D',
    '\x001E', '\x001F', ':',      '*',      '?',      '/',      '\\',     0};

bool IsValidFileName(const CStringW& fileName)
{
	return (fileName.FindOneOf(InvalidFileNameChars) == -1);
}

static const wchar_t InvalidPathChars[] = {
    '"',      '<',      '>',      '|',      '\x0001', '\x0002', '\x0003', '\x0004', '\x0005',
    '\x0006', '\a',     '\b',     '\t',     '\n',     '\v',     '\f',     '\r',     '\x000E',
    '\x000F', '\x0010', '\x0011', '\x0012', '\x0013', '\x0014', '\x0015', '\x0016', '\x0017',
    '\x0018', '\x0019', '\x001A', '\x001B', '\x001C', '\x001D', '\x001E', '\x001F', 0};

bool IsValidPath(const CStringW& path)
{
	if (path.FindOneOf(InvalidPathChars) >= 0)
		return false;

	auto pStr = (LPCWSTR)path;

	if (IsPathAbsolute(path)) // like C:\...
	{
		if (path.GetLength() > 2)
		{
			LPCWSTR pFound = wcspbrk(pStr + 2, L"?:*");
			if (pFound)
				return false;
		}

		return true;
	}

	WCHAR buff[MAX_PATH + 1] = {0};
	if (GetVolumePathNameW(path, buff, MAX_PATH)) // any kind of root
	{
		auto len = wcslen(buff);
		if (len && _wcsnicmp(pStr, buff, len) == 0)
		{
			LPCWSTR pFound = wcspbrk(pStr + len, L"?:*");
			if (pFound)
				return false;
		}
	}

	return true;
}

CStringW TrimForAst(const CStringW& path, int endFragmetsToPreserve /*= 2*/, LPCWSTR prefix /*= L"TRIMMED FOR AST"*/)
{
	for (int i = path.GetLength() - 1; i >= 0; i--)
	{
		if (path[i] == L'\\' || path[i] == L'/')
		{
			if (endFragmetsToPreserve-- == 0)
			{
				if (prefix)
					return prefix + path.Mid(i);

				return path.Mid(i);
			}
		}
	}

	return path;
}

bool HashText(const WTString& text, BYTE outHash[20])
{
	_ASSERTE(outHash);

	if (!outHash)
		return false;

	bool ok = false;

	try
	{
		CryptoPP::SHA1 hasher;
		hasher.Update((const byte*)text.c_str(), text.GetLength() * sizeof(TCHAR));
		hasher.Final(outHash);
		ok = true;
	}
	catch (...)
	{
		// An unknown exception happened
	}

	return ok;
}

bool HashFile(const CStringW& szFilename, BYTE outHash[20])
{
	_ASSERTE(szFilename.GetLength());
	_ASSERTE(outHash);

	if (!outHash)
		return false;

	bool ok = false;

	HANDLE hFile = NULL;

	try
	{
		// Open the file
		hFile = CreateFileW(szFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		                    FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY |
		                        FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_SEQUENTIAL_SCAN,
		                    NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
#define MAX_BUFFER_SIZE 4096
			std::vector<BYTE> bufferVec(MAX_BUFFER_SIZE);
			BYTE* buffer = &bufferVec[0];
			DWORD totalBytesRead = 0;

			CryptoPP::SHA1 hasher;

			for (;;)
			{
				DWORD bytesRead;
				BOOL bSuccess = ReadFile(hFile, buffer, MAX_BUFFER_SIZE, &bytesRead, NULL);
				if (bSuccess && bytesRead > 0)
				{
					hasher.Update(buffer, bytesRead);
					totalBytesRead += bytesRead;
				}
				else
				{
					break;
				}
			}

			if (totalBytesRead)
			{
				hasher.Final(outHash);
				ok = true;
			}
		}
	}
	catch (...)
	{
		// An unknown exception happened
	}

	if (hFile)
		CloseHandle(hFile);

	return ok;
}

bool HashFile(const CStringW& szFilename, DWORD* outHashVal)
{
	if (!outHashVal)
		return false;

	BYTE fullHash[20];
	bool ok = HashFile(szFilename, fullHash);
	if (ok)
	{
		// take the first four bytes of the hash, and xor them for more obfuscation.
		DWORD hash = 0;
		memcpy(&hash, fullHash, sizeof(hash));

#define XOR_MASK 0x3c757cee // = WTHashKey("VISUALASSISTXISTHEBEST");
		*outHashVal = hash ^ XOR_MASK;
	}
	return ok;
}

WTString GetFileHashStr(const CStringW& filePath)
{
	CStringW hashStr;
	if (GetFileHashStr(filePath, hashStr))
		return WTString(hashStr);
	return WTString();
}

bool GetFileHashStr(const CStringW& filePath, CStringW& outHashStr)
{
	outHashStr.Empty();

	const int kHashLen = 20;
	BYTE hash[kHashLen];
	memset(hash, 0, kHashLen);
	if (!HashFile(filePath, &hash[0]))
		return false;

	const int kHashStrLen = kHashLen * 2;
	CString tmp('0', kHashStrLen);
	auto tmpHashStrBuf = tmp.GetBuffer();
	for (int i = 0; i < kHashLen; ++i)
	{
		tmpHashStrBuf += sprintf(tmpHashStrBuf, "%02X", hash[i]);
	}

	outHashStr = tmp;
	return true;
}

bool GetFileLineSortedHashStr(const CStringW& filePath, CStringW& outHashStr)
{
	outHashStr.Empty();

	const int kHashLen = 20;
	BYTE hash[kHashLen];
	memset(hash, 0, kHashLen);

	WTString content;
	if (!content.ReadFile(filePath))
		return false;
	if (content.IsEmpty())
		return false;

	// split on line
	StrVectorA lines;
	WtStrSplitA(content, lines, "\n");
	content = NULLSTR;

	// std::sort(lines.begin(), lines.end());
	Concurrency::parallel_buffered_sort(lines.begin(), lines.end());

	try
	{
		CryptoPP::SHA1 hasher;

		for (const auto& ln : lines)
			hasher.Update((const byte*)ln.c_str(), (uint)ln.GetLength());

		hasher.Final(hash);
	}
	catch (...)
	{
		return false;
	}

	const int kHashStrLen = kHashLen * 2;
	CString tmp('0', kHashStrLen);
	auto tmpHashStrBuf = tmp.GetBuffer();
	for (unsigned char i : hash)
		tmpHashStrBuf += sprintf(tmpHashStrBuf, "%02X", i);

	outHashStr = tmp;
	return true;
}

WTString GetTextHashStr(const WTString& text)
{
	const int kHashLen = 20;
	BYTE hash[kHashLen];
	const int kHashStrLen = kHashLen * 2;
	WTString hashStr('0', kHashStrLen + 1);
	memset(hash, 0, kHashLen);
	if (!HashText(text, &hash[0]))
		return hashStr;

	auto tmpHashStrBuf = hashStr.GetBuffer(kHashStrLen + 1);
	for (int i = 0; i < kHashLen; ++i)
	{
		tmpHashStrBuf += sprintf(tmpHashStrBuf, "%02X", hash[i]);
	}

	hashStr.ReleaseBuffer();
	return hashStr;
}

bool DoesFilenameCaseDiffer(const CStringW& first, const CStringW& second)
{
	// returns true only if full paths are equal, but filenames differ in case
	return !_wcsicmp(first, second) && wcscmp(Basename(first), Basename(second));
}

bool HasUtf8Bom(const CStringW& theFile)
{
	_ASSERTE(IsFile(theFile));

	CFileW hFile;
	if (!hFile.Open(theFile, CFile::modeRead))
		return false;

	const int utf8BomLen = 3;
	const char utf8Bom[utf8BomLen] = {'\xef', '\xbb', '\xbf'};
	char readBuf[utf8BomLen];
	if (hFile.Read(readBuf, utf8BomLen) != utf8BomLen)
		return false;

	return !memcmp(readBuf, utf8Bom, utf8BomLen);
}

#ifndef __IFileDialog_INTERFACE_DEFINED__

typedef struct _COMDLG_FILTERSPEC
{
	LPCWSTR pszName;
	LPCWSTR pszSpec;
} COMDLG_FILTERSPEC;

enum _FILEOPENDIALOGOPTIONS
{
	FOS_OVERWRITEPROMPT = 0x2,
	FOS_STRICTFILETYPES = 0x4,
	FOS_NOCHANGEDIR = 0x8,
	FOS_PICKFOLDERS = 0x20,
	FOS_FORCEFILESYSTEM = 0x40,
	FOS_ALLNONSTORAGEITEMS = 0x80,
	FOS_NOVALIDATE = 0x100,
	FOS_ALLOWMULTISELECT = 0x200,
	FOS_PATHMUSTEXIST = 0x800,
	FOS_FILEMUSTEXIST = 0x1000,
	FOS_CREATEPROMPT = 0x2000,
	FOS_SHAREAWARE = 0x4000,
	FOS_NOREADONLYRETURN = 0x8000,
	FOS_NOTESTFILECREATE = 0x10000,
	FOS_HIDEMRUPLACES = 0x20000,
	FOS_HIDEPINNEDPLACES = 0x40000,
	FOS_NODEREFERENCELINKS = 0x100000,
	FOS_DONTADDTORECENT = 0x2000000,
	FOS_FORCESHOWHIDDEN = 0x10000000,
	FOS_DEFAULTNOMINIMODE = 0x20000000,
	FOS_FORCEPREVIEWPANEON = 0x40000000
};

using FILEOPENDIALOGOPTIONS = DWORD;
using FDAP = DWORD;

MIDL_INTERFACE("42f85136-db7e-439c-85f1-e4075d135fc8")
IFileDialog : public IModalWindow
{
  public:
	virtual HRESULT STDMETHODCALLTYPE SetFileTypes(
	    /* [in] */ UINT cFileTypes,
	    /* [size_is][in] */ __RPC__in_ecount_full(cFileTypes) const COMDLG_FILTERSPEC* rgFilterSpec) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFileTypeIndex(
	    /* [in] */ UINT iFileType) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFileTypeIndex(
	    /* [out] */ __RPC__out UINT * piFileType) = 0;
	virtual HRESULT STDMETHODCALLTYPE Advise(
	    /* [in] */ __RPC__in_opt IFileDialogEvents * pfde,
	    /* [out] */ __RPC__out DWORD * pdwCookie) = 0;
	virtual HRESULT STDMETHODCALLTYPE Unadvise(
	    /* [in] */ DWORD dwCookie) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetOptions(
	    /* [in] */ FILEOPENDIALOGOPTIONS fos) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetOptions(
	    /* [out] */ __RPC__out FILEOPENDIALOGOPTIONS * pfos) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDefaultFolder(
	    /* [in] */ __RPC__in_opt IShellItem * psi) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFolder(
	    /* [in] */ __RPC__in_opt IShellItem * psi) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFolder(
	    /* [out] */ __RPC__deref_out_opt IShellItem * *ppsi) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentSelection(
	    /* [out] */ __RPC__deref_out_opt IShellItem * *ppsi) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFileName(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszName) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFileName(
	    /* [string][out] */ __RPC__deref_out_opt_string LPWSTR * pszName) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetTitle(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszTitle) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetOkButtonLabel(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszText) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFileNameLabel(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszLabel) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetResult(
	    /* [out] */ __RPC__deref_out_opt IShellItem * *ppsi) = 0;
	virtual HRESULT STDMETHODCALLTYPE AddPlace(
	    /* [in] */ __RPC__in_opt IShellItem * psi,
	    /* [in] */ FDAP fdap) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDefaultExtension(
	    /* [string][in] */ __RPC__in_string LPCWSTR pszDefaultExtension) = 0;
	virtual HRESULT STDMETHODCALLTYPE Close(
	    /* [in] */ HRESULT hr) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetClientGuid(
	    /* [in] */ __RPC__in REFGUID guid) = 0;
	virtual HRESULT STDMETHODCALLTYPE ClearClientData(void) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetFilter(
	    /* [in] */ __RPC__in_opt IShellItemFilter * pFilter) = 0;
};

#endif

#ifndef __IFileOpenDialog_INTERFACE_DEFINED__

MIDL_INTERFACE("d57c7288-d4ad-4768-be02-9d969532d960")
IFileOpenDialog : public IFileDialog
{
  public:
	virtual HRESULT STDMETHODCALLTYPE GetResults(
	    /* [out] */ __RPC__deref_out_opt IShellItemArray * *ppenum) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetSelectedItems(
	    /* [out] */ __RPC__deref_out_opt IShellItemArray * *ppsai) = 0;
};

#endif

CStringW VaGetOpenFileName(
    HWND owner, LPCWSTR title, LPCWSTR initDir, LPCWSTR filter, int filterIndex /*= 1*/,
    DWORD ofn_flags /*= OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ENABLESIZING*/)
{
	CStringW result;

	struct FocusRestore
	{
		HWND focus;

		FocusRestore()
		{
			focus = ::GetFocus();
		}

		~FocusRestore()
		{
			::SetFocus(focus);
		}
	} _restore_focus;

	CFileDialog dlg(TRUE);
	CComPtr<IFileOpenDialog> pFileOpen = dlg.GetIFileOpenDialog();
	if (pFileOpen)
	{
		// set title for the dialog
		pFileOpen->SetTitle(title);

		// set initial folder
		CComPtr<IShellItem> si_initFolder;
		if (SUCCEEDED(SHCreateItemFromParsingName(initDir, nullptr, IID_IShellItem,
		                                          reinterpret_cast<void**>(&si_initFolder))) &&
		    si_initFolder)
			pFileOpen->SetFolder(si_initFolder);

		// set options
		DWORD fos_flags = 0;
		if (SUCCEEDED(pFileOpen->GetOptions(&fos_flags)))
		{
#ifndef OFN_TO_FOS
#define OFN_TO_FOS(FLAG)                                                                                               \
	if (ofn_flags & (OFN_##FLAG))                                                                                      \
		fos_flags |= (FOS_##FLAG);                                                                                     \
	else                                                                                                               \
		fos_flags &= ~(FOS_##FLAG);

			OFN_TO_FOS(ALLOWMULTISELECT)
			OFN_TO_FOS(CREATEPROMPT)
			OFN_TO_FOS(DONTADDTORECENT)
			OFN_TO_FOS(FILEMUSTEXIST)
			OFN_TO_FOS(FORCESHOWHIDDEN)
			OFN_TO_FOS(NOCHANGEDIR)
			OFN_TO_FOS(NODEREFERENCELINKS)
			OFN_TO_FOS(NOREADONLYRETURN)
			OFN_TO_FOS(NOTESTFILECREATE)
			OFN_TO_FOS(NOVALIDATE)
			OFN_TO_FOS(OVERWRITEPROMPT)
			OFN_TO_FOS(PATHMUSTEXIST)
			OFN_TO_FOS(SHAREAWARE)
#undef OFN_TO_FOS
#endif
			fos_flags |= FOS_FORCEFILESYSTEM; // to mimic GetOpenFileName behavior
			pFileOpen->SetOptions(fos_flags);
		}

		// split filter string into array of COMDLG_FILTERSPEC
		std::vector<COMDLG_FILTERSPEC> _specs;
		LPCWSTR f_str = filter;
		while (f_str && *f_str)
		{
			// get name length
			size_t name_len = wcslen(f_str);
			if (name_len)
			{
				COMDLG_FILTERSPEC spec;
				spec.pszName = f_str;  // assign the name
				f_str += name_len + 1; // move start to spec

				// get spec length
				size_t spec_len = wcslen(f_str);
				if (spec_len)
				{
					spec.pszSpec = f_str;  // assign the spec
					f_str += spec_len + 1; // move start to next name

					_specs.push_back(spec);
				}
				else
					break;
			}
			else
				break;
		}

		// set file types => filter
		if (!_specs.empty())
		{
			// assign filter to the dialog
			if (SUCCEEDED(pFileOpen->SetFileTypes((uint)_specs.size(), &_specs.front())))
			{
				if (filterIndex < 1 || filterIndex > (int)_specs.size())
					filterIndex = 1;

				pFileOpen->SetFileTypeIndex((UINT)filterIndex);
			}
		}

		// show the dialog
		if (S_OK == pFileOpen->Show(owner))
		{
			// if OK, get the file name from the dialog
			CComPtr<IShellItem> pItem;
			if (SUCCEEDED(pFileOpen->GetResult(&pItem)))
			{
				PWSTR pszFilePath = nullptr;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath)
				{
					// assign the file name to the result
					result = pszFilePath;
					CoTaskMemFree(pszFilePath);
				}
			}
		}

		return result;
	}

	// if not Vista+ or some error occurred
	// use Win32 API to open the dialog box

	WCHAR path[MAX_PATH];
	::ZeroMemory(path, sizeof(path));

	OPENFILENAMEW ofn;
	::ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = (DWORD)filterIndex;
	ofn.lpstrFileTitle = nullptr;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = initDir;
	ofn.lpstrTitle = title;
	ofn.Flags = ofn_flags;

	if (::GetOpenFileNameW(&ofn) == TRUE)
	{
		DWORD length = ::GetLongPathNameW(path, nullptr, 0);
		if (length)
		{
			LPWSTR buff = result.GetBufferSetLength((int)length);
			if (GetLongPathNameW(path, buff, length + 1))
			{
				result.ReleaseBuffer((int)length);
				return result;
			}
			result.ReleaseBuffer(0);
		}
	}

	return result;
}
