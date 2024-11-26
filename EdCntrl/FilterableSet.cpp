#include "StdAfxEd.h"
#include <ppl.h>
#include "FilterableSet.h"
#include "wtcsym.h"
#include "WTString.h"
#include "parse.h"
#include "Mparse.h"
#include "log.h"
#include "file.h"
#include "fdictionary.h"
#include "project.h"
#include "TokenW.h"
#include "VASeException\VASeException.h"
#include "Directories.h"
#include "FileTypes.h"
#include "Settings.h"
#include "StringUtils.h"
#include "FileId.h"
#include "vaIPC\vaIPC\common\string_utils.h"
#include <execution>
#include "ltalloc/ltalloc_va.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void GetDsItemSymLocation(LPCSTR pStr, WTString& fileIdAndLineNum);
void GetDsItemSymDef(LPCSTR pStr, WTString& def);

FilteredStringList::~FilteredStringList()
{
	Empty();
}

bool HaveSameExclusionFilters(WTString f1, WTString f2)
{
	StrVectorA set1, set2;
	WtStrSplitA(f1, set1, " ");
	WtStrSplitA(f2, set2, " ");

	// remove from vectors items that don't contain '-'
	StrVectorA::iterator it1, it2;
	for (it1 = set1.begin(); it1 != set1.end();)
	{
		if ((*it1).Find('-') == -1)
			it1 = set1.erase(it1);
		else
			++it1;
	}

	for (it2 = set2.begin(); it2 != set2.end();)
	{
		if ((*it2).Find('-') == -1)
			it2 = set2.erase(it2);
		else
			++it2;
	}

	if (set1.size() != set2.size())
	{
		// different number of exclusion filters
		return false;
	}

	it1 = set1.begin();
	it2 = set2.begin();
	for (; it1 != set1.end(); ++it1, ++it2)
	{
		if ((*it1) != (*it2))
		{
			// this exclusion filter doesn't match
			return false;
		}
	}

	return true;
}

void FilteredStringList::Empty()
{
	AutoLockCs l(mCs);
	FilterableStringSet::Empty();
	for (StrVector::iterator it(mAllocPool.begin()); it != mAllocPool.end(); ++it)
		delete[] (*it);
	mAllocPool.clear();
}

void FilteredStringList::ReadDBFile(const CStringW& dbFile)
{
	CFileW idxFile;
	if (!idxFile.Open(dbFile, CFile::modeRead | CFile::typeBinary | CFile::shareDenyNone))
		return;

	const DWORD flen = (DWORD)idxFile.GetLength();
	if (!flen)
		return;

	char* rawData = new char[flen + 1];
	if (!rawData)
	{
		vLog("ERROR: memory allocation failed in FilteredStringList sz(%lu)", flen);
		return;
	}

	{
		AutoLockCs l(mCs);
		mAllocPool.push_back(rawData);
	}

#if _MSC_VER <= 1200
	/*DWORD nRead =*/idxFile.ReadHuge(rawData, flen);
#else
	/*DWORD nRead =*/(DWORD) idxFile.Read(rawData, flen);
#endif
	rawData[flen] = '\0';

	_ASSERTE(rawData[0] == '\n');
	AddItem(&rawData[1]); // skip \n
	for (LPTSTR p = rawData; p; p = strstr(&p[1], "\n"))
	{
		// null terminate each individual entry since the pointers are passed
		// into string object constructors that rely on \0 instead of \n.
		*p = '\0';
		AddItem(&p[1]);
		// restore for the GetDsItemSym* functions
		*p = '\n';
	}
}

void FilteredStringList::ReadDBDir(const CStringW& dbDir)
{
	FileList files;
	FindFiles(dbDir, L"*Ds.db", files);
	auto readFn = [this](const FileInfo& fi) { ReadDBFile(fi.mFilename); };

#pragma warning(push)
#pragma warning(disable : 4127)
	if (true || Psettings->mUsePpl)
		Concurrency::parallel_for_each(files.begin(), files.end(), readFn);
	else
		std::for_each(files.begin(), files.end(), readFn);
#pragma warning(pop)
}

void FilteredStringList::ReadDB(BOOL restrictToWorkspace /*= true*/)
{
	Empty();
	CStringW projDbDir;
	if (GlobalProject)
		projDbDir = GlobalProject->GetProjDbDir();

	if (!projDbDir.IsEmpty())
		ReadDBDir(projDbDir);

	if (restrictToWorkspace)
		return;

	FileDic* sysDic = ::GetSysDic();
	// goto doesn't work for .net symbols, so don't add them
	if (sysDic && sysDic != g_pCSDic)
	{
		CStringW sysDir(sysDic->GetDBDir());
		if (!sysDir.IsEmpty())
			ReadDBDir(sysDir);
	}

	{
		// [case: 97175]
		const CStringW slnSysDir(projDbDir + kDbIdDir[DbTypeId_SolutionSystemCpp]);
		if (IsDir(slnSysDir))
			ReadDBDir(slnSysDir);
	}

	if (Psettings->mFsisAltCache)
	{
		const CStringW cacheDir(VaDirs::GetDbDir() + L"cache");
		if (IsDir(cacheDir))
			ReadDBDir(cacheDir);
	}
}

void FilteredStringList::RemoveDuplicates()
{
	AutoLockCs l(mCs);
	if (!mAllItemCount)
		return;

#ifdef _DEBUGxx
	// dump to file
	CFileW f;
	if (f.Open(L"d:\\fsis.txt", CFile::modeCreate | CFile::shareDenyWrite | CFile::modeWrite | CFile::modeNoInherit))
	{
		const int count = mAllItemCount;
		for (int i = 0; i < count; ++i)
		{
			const LPTSTR p = (const LPTSTR)GetOrgAt(i);
			if (!p)
				continue;

			const LPTSTR pEnd = strstr(p, "\n");
			if (pEnd)
			{
				*pEnd = '\0';

				// print p here
				f.Write(p, pEnd - p);
				f.Write("\r\n", 2);

				*pEnd = '\n';
			}
			else
			{
				f.Write(p, strlen(p));
				f.Write("\r\n", 2);
			}
		}
	}
#endif // _DEBUG

	try
	{
		int j = 0;
		const int count = mAllItemCount;
		for (int i = 1; i < count; i++)
		{
			const LPCSTR p1 = GetOrgAt(j);
			const LPCSTR p2 = GetOrgAt(i);
			int x = 0;
			for (; p1[x] == p2[x] && p1[x] != DB_FIELD_DELIMITER; x++)
				;

			if (p1[x] != DB_FIELD_DELIMITER || p2[x] != DB_FIELD_DELIMITER)
			{
				mAllItems[++j] = mAllItems[i];
			}
			else
			{
				// same sym name, check for allowable dupe
				int type1, db1;
				::GetDsItemSymTypeAndDb(p1, type1, db1);
				if (LINQ_VAR != type1)
				{
					int type2, db2;
					::GetDsItemSymTypeAndDb(p2, type2, db2);
					if (LINQ_VAR != type2)
					{
						// only allow dupes for VA_DB_Solution items due to noise in sys db
						if (VA_DB_Solution == db1 || VA_DB_Solution == db2)
						{
							if (GOTODEF == type1 || GOTODEF == type2)
							{
								// FSIS prefers definition
								if (GOTODEF == type1 && GOTODEF == type2)
								{
									WTString fileAndLine1, fileAndLine2;
									::GetDsItemSymLocation(p1, fileAndLine1);
									::GetDsItemSymLocation(p2, fileAndLine2);
									if (fileAndLine1 != fileAndLine2)
									{
										// keep both GOTODEF since at different places (overloaded func)
										mAllItems[++j] = mAllItems[i];
									}
								}
								else if (GOTODEF == type1)
								{
									// keep the first (GOTODEF), skip the second (FUNC?)
								}
								else if (GOTODEF == type2)
								{
									// overwrite the first (FUNC?) with the second (GOTODEF)
									mAllItems[j] = mAllItems[i];
								}
							}
							else if (type1 == type2)
							{
								// same name and type
								if (CLASS == type1 || STRUCT == type1 || C_INTERFACE == type1)
								{
									// [case: 54823] allow class/struct dupes
									mAllItems[++j] = mAllItems[i];
								}
								else if (FUNC == type1)
								{
									WTString fileAndLine1, fileAndLine2;
									::GetDsItemSymLocation(p1, fileAndLine1);
									::GetDsItemSymLocation(p2, fileAndLine2);
									if (fileAndLine1 != fileAndLine2)
									{
										// check def
										WTString def1, def2;
										::GetDsItemSymDef(p1, def1);
										::GetDsItemSymDef(p2, def2);
										bool isGotoDef1 = def1.Find("{...}") != -1 || def1.EndsWith("default");
										bool isGotoDef2 = def2.Find("{...}") != -1 || def2.EndsWith("default");
										if (isGotoDef1 && isGotoDef2)
										{
											// keep both GOTODEF since at different places (overloaded func)
											mAllItems[++j] = mAllItems[i];
										}
										else if (isGotoDef1)
										{
											// keep the first (GOTODEF), skip the second (FUNC)
										}
										else if (isGotoDef2)
										{
											// overwrite the first (FUNC) with the second (GOTODEF)
											mAllItems[j] = mAllItems[i];
										}
										else
										{
											// keep both FUNC since at different places (overloaded func)
											mAllItems[++j] = mAllItems[i];
										}
									}
								}
								// #ifdef _DEBUG
								//								else if (NAMESPACE != type1 &&
								//									C_ENUM != type1 &&
								//									VAR != type1 &&
								//									TEMPLATETYPE != type1 &&
								//									DEFINE != type1 &&
								//									EVENT != type1 &&
								//									PROPERTY != type1)
								//								{
								//									// what other things show up with duplicate types?
								//									_asm nop;
								//								}
								// #endif // _DEBUG
							}
							else
							{
								// duplicate name but different types (other than GOTODEF and its match)?
								// TYPE/FUNC, FUNC/VAR, CLASS/NAMESPACE
								// INTERFACE/STRUCT/TYPE - important for COM interfaces that have 3 decls
								WTString fileAndLine1, fileAndLine2;
								::GetDsItemSymLocation(p1, fileAndLine1);
								::GetDsItemSymLocation(p2, fileAndLine2);
								if (fileAndLine1 != fileAndLine2)
									mAllItems[++j] = mAllItems[i];
							}
						}
					}
				}
			}
		}

		_ASSERTE(j + 1 <= mCapacity);
		mAllItemCount = j + 1;
	}
	catch (...)
	{
		VALOGEXCEPTION("FilteredStringList::RemoveDupes:Err");
	}
}

static DWORD HexCharToDword(char ch)
{
	switch (ch)
	{
	case '0':
		return 0;
	case '1':
		return 1;
	case '2':
		return 2;
	case '3':
		return 3;
	case '4':
		return 4;
	case '5':
		return 5;
	case '6':
		return 6;
	case '7':
		return 7;
	case '8':
		return 8;
	case '9':
		return 9;
	case 'a':
	case 'A':
		return 10;
	case 'b':
	case 'B':
		return 11;
	case 'c':
	case 'C':
		return 12;
	case 'd':
	case 'D':
		return 13;
	case 'e':
	case 'E':
		return 14;
	case 'f':
	case 'F':
		return 15;

	default:
		return 0;
	}
}

static DWORD HexCharsToDword(char ch1, char ch2)
{
	return (HexCharToDword(ch1) << 4) | HexCharToDword(ch2);
}

void FilteredStringList::AddItem(LPCSTR strPtr)
{
	// [case: 141840]
	// prevent assert due to known bugs, restore when they are fixed
	// _ASSERTE(((strPtr[0] == '0' || strPtr[0] == '1') && strPtr[1] == DB_FIELD_DELIMITER) || (strPtr[0] == '#'));
	if (strPtr[0] == '0' || strPtr[0] == '#')
	{
		// not user visible.
		// #includes shouldn't be getting written to the ds files -- something to look into at some point
		return;
	}
	else if (strPtr[0] != '1')
	{
		// [case: 141840] [case: 141846]
		// this is the result of some sort of parse error where a dsfile entry has data
		// that was not properly scrubbed of line breaks
		return;
	}

	// skip over visibility and first delimiter
	strPtr += 2;

	if (strPtr[0] != ':')
	{
		// this happens for asp code - open FSIS in ast solution.
		// looks like a bug in the html parser.
		// see //ProductSource/VAProduct/VABuildTrunk/tests/EdTests/expectedResults/cache/Local/outlineTest.asp_ds.db
		return;
	}

	if (!ISCSYM(strPtr[1]))
		return; // exclude operators

	if (::wt_isdigit(strPtr[1]))
	{
		// [case: 141840] [case: 141847]
		// this is the result of some sort of parse error where a dsfile entry either
		// has invalid scope or is malformed
		// if (StartsWith(strPtr, ":0:")) then the entry is likely for a local sym
		return;
	}

	if (stfOnlyTypes == mSymbolTypes)
	{
		// [case: 37054]
		const char* p = strPtr;
		const char* p2;

		p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
		if (!p++)
			return;

		p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
		if (!p++)
			return;

		// Type and db flags
		p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
		if (!p++)
			return;

		p2 = p - 2;

		// sscanf is too slow
		DWORD type = ::HexCharsToDword(*(p2 - 1), *p2) & TYPEMASK;

		// don't use IS_OBJECT_TYPE because of noise due to TAG and TEMPLATETYPE
		switch (type)
		{
		case CLASS:
		case STRUCT:
			// [case: 52463]
			//		case TYPE:
			// 		case C_ENUM:
		case C_INTERFACE:
		case NAMESPACE:
		case MODULE:
			break;

		default:
			return;
		}
	}

	AutoLockCs l(mCs);
	if (mAllItemCount >= mCapacity) // time to realloc() some more
	{
		if (!IncreaseCapacity())
			return;
	}

	mAllItems[mAllItemCount++] = strPtr;
}

LPCSTR
GetDsItemFileId(const LPCSTR p1, int& len, int& lineLen)
{
	if (!p1 || p1[0] != ':')
	{
		// this happens for asp code - open FSIS in ast solution.
		// looks like a bug in the html parser.
		// see //ProductSource/VAProduct/VABuildTrunk/tests/EdTests/expectedResults/cache/Local/outlineTest.asp_ds.db
		return nullptr;
	}

	const char* p = p1;

	// def
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return nullptr;

	// type and db flags
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return nullptr;

	// attributes
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return nullptr;

	// fileId
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return nullptr;

	// lineNum
	const char* pNext = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!pNext)
		return nullptr;

	len = ptr_sub__int(pNext++, p);

#if defined(VA_CPPUNIT) || defined(_DEBUG)
	const char* pNext2 = _tcschr(pNext, (unsigned int)DB_FIELD_DELIMITER);
#else
	const char* pNext2 = _tcschr(pNext, '\n');
#endif
	if (!pNext2)
	{
		// last row of file
		size_t len2 = _tcslen(pNext);
		pNext2 = pNext + len2;
	}

	lineLen = ptr_sub__int(pNext2, pNext);

	return p;
}

int CompareDsItemFileIdsAndGetLine(LPCSTR p1, LPCSTR p2, WTString& ln1, WTString& ln2)
{
	// compare fileid strings without constructing allocating/deallocating strings
	int p1Len, p2Len, p1LineLen, p2LineLen;
	p1LineLen = p2LineLen = 0;
	LPSTR p1f = (LPSTR)GetDsItemFileId(p1, p1Len, p1LineLen);
	LPSTR p2f = (LPSTR)GetDsItemFileId(p2, p2Len, p2LineLen);
	if (!p1f && !p2f)
		return ptr_sub__int(p1, p2);
	if (!p1f)
		return -1;
	if (!p2f)
		return 1;

	p1f[p1Len] = '\0';
	p2f[p2Len] = '\0';
	const int fc = strcmp(p1f, p2f);
	p1f[p1Len] = DB_FIELD_DELIMITER;
	p2f[p2Len] = DB_FIELD_DELIMITER;

	if (!fc)
	{
		// only construct line num strings if file compare was equal
		if (p1LineLen)
			ln1 = WTString(p1f + 1 + p1Len, p1LineLen);
		if (p2LineLen)
			ln2 = WTString(p2f + 1 + p2Len, p2LineLen);
	}

	return fc;
}

bool FilteredSetStringCompareNoCase(const LPCSTR& v1, const LPCSTR& v2, int partialMatchLen)
{
	LPCSTR n1 = v1, n2 = v2;
	if (n1 == NULL || n2 == NULL)
		return 0;

	//
	// exactly the same as _strnicmp, except we also treat DB_FIELD_DELIMITER as a deliminator
	//
	if (partialMatchLen)
	{
		int c1 = 0;
		int c2 = 0;

		do
		{
			c1 = (unsigned char)(*(n1++));
			if (c1 == (unsigned char)DB_FIELD_DELIMITER)
				c1 = 0;
			else if ((c1 >= 'A') && (c1 <= 'Z'))
				c1 -= 'A' - 'a';

			c2 = (unsigned char)(*(n2++));
			if (c2 == (unsigned char)DB_FIELD_DELIMITER)
				c2 = 0;
			else if ((c2 >= 'A') && (c2 <= 'Z'))
				c2 -= 'A' - 'a';
		} while (--partialMatchLen && c1 && (c1 == c2));

		if (c1 == c2)
		{
			// case-insensitive dupes.
			// to get a consistent sort order, compare fileId
			WTString ln1, ln2;
			int fc = ::CompareDsItemFileIdsAndGetLine(v1, v2, ln1, ln2);
			if (fc == 0)
			{
				// same file, now compare line
				int len1 = ln1.GetLength(), len2 = ln2.GetLength();
				if (len1 == len2)
				{
					// if str lens are equal, then need to do str compare
					// assumes digits sort before hex alphas
					fc = ln1.Compare(ln2.c_str());
					if (0 == fc)
					{
						// last resort, compare pointer address
						return (v1 - v2) < 0;
					}
					return fc < 0;
				}

				// shorter string represents smaller number (fewer digits)
				return len1 < len2;
			}

			return fc < 0;
		}

		return (c1 - c2) < 0;
	}
	else
	{
		return 0;
	}
}

void GetDsItemSymTypeAndDb(LPCSTR pStr, int& symType, int& vaDb)
{
	symType = vaDb = 0;
	if (!pStr || pStr[0] != ':')
	{
		// this happens for asp code - open FSIS in ast solution.
		// looks like a bug in the html parser.
		// see //ProductSource/VAProduct/VABuildTrunk/tests/EdTests/expectedResults/cache/Local/outlineTest.asp_ds.db
		return;
	}

	const char* p = pStr;

	// def
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// type and db flags
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// NOTE: assumes DB uses only 2 bytes (MSBs)
	DWORD val = ::HexCharsToDword(*p, *(p + 1));
	val <<= 24;
	vaDb = int(val & (VA_DB_FLAGS & ~VA_DB_BackedByDatafile));

	// attributes
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// rewind
	// NOTE: assumes type use only 2 bytes (LSBs)
	const char* p2 = p - 2;

	// sscanf is too slow
	val = ::HexCharsToDword(*(p2 - 1), *p2);
	symType = int(val & TYPEMASK);
}

void GetDsItemSymLocation(LPCSTR pStr, CStringW& file, int& line)
{
	file.Empty();
	line = 0;
	if (!pStr || pStr[0] != ':')
	{
		// this happens for asp code - open FSIS in ast solution.
		// looks like a bug in the html parser.
		// see //ProductSource/VAProduct/VABuildTrunk/tests/EdTests/expectedResults/cache/Local/outlineTest.asp_ds.db
		return;
	}

	const char* p = pStr;

	// def
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// type and db flags
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// attributes
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// [case: 96920]
	// don't use DB_FIELD_DELIMITER in sscanf when built in vs2015
	// fileId
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	uint fid = 0;
	sscanf(p, "%x", &fid);
	file = gFileIdManager->GetFileForUser(fid);

	// line num
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	sscanf(p, "%x", &line);
}

void GetDsItemSymLocation(LPCSTR pStr, WTString& fileIdAndLineNum)
{
	if (!pStr || pStr[0] != ':')
	{
		// this happens for asp code - open FSIS in ast solution.
		// looks like a bug in the html parser.
		// see //ProductSource/VAProduct/VABuildTrunk/tests/EdTests/expectedResults/cache/Local/outlineTest.asp_ds.db
		return;
	}

	const char* p = pStr;

	// def
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// type and db flags
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// attributes
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// fileId
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// lineNum
	const char* p2 = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p2)
		return;

		// end
#if defined(VA_CPPUNIT) || defined(_DEBUG)
	const char* p3 = _tcschr(p2 + 1, (unsigned int)DB_FIELD_DELIMITER);
#else
	const char* p3 = _tcschr(p2 + 1, '\n');
#endif
	if (!p3)
	{
		size_t len = _tcslen(p2);
		p3 = p2 + len;
	}

	fileIdAndLineNum = WTString(p, ptr_sub__int(p3, p));
}

void GetDsItemSymDef(LPCSTR pStr, WTString& def)
{
	if (!pStr || pStr[0] != ':')
	{
		// this happens for asp code - open FSIS in ast solution.
		// looks like a bug in the html parser.
		// see //ProductSource/VAProduct/VABuildTrunk/tests/EdTests/expectedResults/cache/Local/outlineTest.asp_ds.db
		return;
	}

	const char* p = pStr;

	// def
	p = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p++)
		return;

	// type and db flags
	const char* p2 = _tcschr(p, (unsigned int)DB_FIELD_DELIMITER);
	if (!p2)
		return;

	def = WTString(p, ptr_sub__int(p2, p));
}

void GetDsItemTypeAndAttrsAfterText(LPCSTR pStr, UINT& type, UINT& attrs)
{
	_ASSERTE(pStr && *pStr);
	type = attrs = 0;
	pStr = strchr(++pStr, DB_FIELD_DELIMITER);
	pStr = strchr(++pStr, DB_FIELD_DELIMITER);
	pStr++;

	// [case: 96920]
	// don't use DB_FIELD_DELIMITER in sscanf when built in vs2015
	sscanf(pStr, "%x", &type);
	_ASSERTE((type & VA_DB_FLAGS_MASK) == (type & TYPEMASK));
	type &= TYPEMASK;

	pStr = _tcschr(pStr, (unsigned int)DB_FIELD_DELIMITER);
	if (!pStr++)
		return;

	sscanf(pStr, "%x", &attrs);
}

template<typename CH>
bool HaveSameExclusionFiltersT(std::basic_string_view<CH> f1, std::basic_string_view<CH> f2)
{
	/*static*/ const std::basic_string<CH> space({CH{' '}});
	auto set1 = WtStrSplit<CH>(f1, space);
	auto set2 = WtStrSplit<CH>(f2, space);

	// remove from vectors items that don't contain '-'
	std::erase_if(set1, [](const auto& s) { return s.find_first_of(CH{'-'}) == s.npos; });
	std::erase_if(set2, [](const auto& s) { return s.find_first_of(CH{'-'}) == s.npos; });

	if (set1.size() != set2.size())
		return false;

	std::sort(set1.begin(), set1.end());
	std::sort(set2.begin(), set2.end());
	return set1 == set2;
}

// this is the template version of StrIsMultiMatchPattern
// created for FilterableSet::FilterFiles #FilterFiles
template <typename CH>
bool StrIsMultiMatchPatternT(const std::basic_string<CH>& pat)
{
	// Find the first occurrence of a space or comma character.
	auto pos = pat.find_first_of(CH(' '), 0);
	if (pos == std::basic_string<CH>::npos)
		pos = pat.find_first_of(CH(','), 0);

	// If either a space or comma was found, return true. Otherwise, return false.
	return pos != std::basic_string<CH>::npos;
}

// this is the template version of DoStrMultiMatchRanked
// created for FilterableSet::FilterFiles #FilterFiles
template <typename CH>
int DoStrMultiMatchRankedT(const std::basic_string_view<CH>& str, const std::basic_string<CH>& pat, StrMatchTempStorageT<CH>& temp_storage, bool matchBothSlashAndBackslash, bool use_fuzzy)
{
	int rank = 0;
	std::basic_string<CH> subPat;
	TokenT<CH> patTok(pat, {' '});
	while (patTok.more())
	{
		subPat = patTok.read();
		if (subPat.empty())
			break;

		StrMatchOptionsT<CH> opts(subPat);
		if (!use_fuzzy)
			opts.mFuzzy = use_fuzzy;
		const int curPatRank = StrMatchRankedT<CH>(str, opts, temp_storage, matchBothSlashAndBackslash);
		if (!curPatRank)
			return 0;
		else if (!rank)
			rank = curPatRank; // only possible for the first pattern
		else
			rank = std::min(rank, curPatRank);
	}
	return rank;
}

// this is the template version of StrMultiMatchRanked
// created for FilterableSet::FilterFiles #FilterFiles
template <typename CH>
int StrMultiMatchRankedT(const std::basic_string_view<CH>& str, const std::basic_string<CH>& pat, StrMatchTempStorageT<CH>& temp_storage, bool matchBothSlashAndBackslash, bool use_fuzzy)
{
	TokenT<CH> patTok(pat, {','});
	while (patTok.more())
	{
		std::basic_string<CH> subPat = patTok.read();
		if (!subPat.empty())
		{
			int found = DoStrMultiMatchRankedT<CH>(str, subPat, temp_storage, matchBothSlashAndBackslash, use_fuzzy);
			if (found)
				return found;
		}
	}

	return 0;
}

template <typename CH, typename T>
void FilterableSet<CH, T>::Filter(std::basic_string_view<CH> newFilterIn, bool use_fuzzy, bool force /*= false*/, std::optional<bool> searchFullPath /*= {}*/)
{
	AutoLockCs l(mCs);
	std::basic_string<CH> newFilter(newFilterIn);
	const std::basic_string<CH> prevFilter(mFilter);

	// trim trailing '-' or trailing ' ' (incomplete filter)
	trim_right(newFilter, CH{'-'});
	trim(newFilter);
	if (Psettings->mEnableFilterStartEndTokens && newFilter == std::basic_string<CH>({'.'}))
	{
		// [case: 86713] treat lone . as an empty filter
		newFilter.clear();
	}

	if (newFilter == prevFilter && mFilteredSetCount && !force)
		return;

	mSuggestedItemIdx = 0;
	mFilter = newFilter;
	if (mFilter.empty())
	{
		mFilteredSetCount = mAllItemCount;
		std::copy(mAllItems, mAllItems + mAllItemCount, mFilteredSet);
		return;
	}

	StrMatchOptionsT<CH> opts({});
	if (!use_fuzzy)
		opts.mFuzzy = std::nullopt;
	bool filterCurrentSet = false;
	if ((mFilteredSetCount > 0) && !opts.GetFuzzy() && !opts.GetFuzzyLite() && prevFilter.size() && (prevFilter.size() <= newFilter.size()) &&
	    newFilter.starts_with(prevFilter) && !force)
	{
		filterCurrentSet = true;

		// typing a union filter can add items to result set
		if (mFilter.find(CH{','}) != mFilter.npos)
			filterCurrentSet = false; // could have something like HaveSameExclusionFilters here
		// typing a negative filter can add items to result set (-c will exclude more than -cp)
		else if (mFilter.find(CH{'-'}) != mFilter.npos)
			filterCurrentSet = HaveSameExclusionFiltersT<CH>(prevFilter, mFilter);
	}

		std::vector<std::basic_string<CH>> inclusivePatterns, exclusivePatterns;
		const bool kIsMultiPattern = StrIsMultiMatchPattern<CH>(mFilter);
		if (kIsMultiPattern)
		{
			static const std::basic_string<CH> chars{CH{' '}};
			auto allPatterns = WtStrSplit<CH>(mFilter, chars);

			// sort into inclusive or exclusive
			for (const auto& _cur : allPatterns)
			{
				auto cur = StrMatchOptionsT<CH>::TweakFilter2(_cur, searchFullPath ? *searchFullPath : false, use_fuzzy ? !searchFullPath : true);
				if (cur.empty())
					continue;

				((cur.find('-') == cur.npos) ? inclusivePatterns : exclusivePatterns).emplace_back(cur);
			}
		}
		else
		{
			auto cur = StrMatchOptionsT<CH>::TweakFilter2(mFilter, false, use_fuzzy ? !searchFullPath : true);
			if (!cur.empty())
				inclusivePatterns.emplace_back(std::move(cur));
		}

		for (int patternsGroupIdx = 0; patternsGroupIdx < 3; ++patternsGroupIdx)
		{
			std::vector<std::basic_string<CH>>* patterns;
			switch (patternsGroupIdx)
			{
			case 0:
				patterns = &inclusivePatterns;
				break;
			case 1:
				if (exclusivePatterns.empty())
				return;

				patterns = &exclusivePatterns;
				break;
			case 2:
				// reset mSuggestedItemIdx to first inclusive pattern match
				patterns = &inclusivePatterns;
				break;
			default:
				_ASSERTE(!"bad loop");
				return;
			}

			std::basic_string<CH> temp_item_string_storage;
			std::vector<int8_t> smatches;
			for (const auto& curPattern : *patterns)
			{
				const uint32_t upperLimit = uint32_t(filterCurrentSet ? mFilteredSetCount : mAllItemCount);
				T* sourceArray = filterCurrentSet ? mFilteredSet : mAllItems;
				int8_t matchRank = -1;
				opts.Update(curPattern, false, true);

				smatches.resize((uint32_t)upperLimit);
			bool use_multithread = opts.mEnableFuzzyMultiThreading && IsItemToTextMultiThreadSafe();
				if (use_multithread)
				{
					constexpr uint32_t batch_size = 1024;
					const uint32_t batches = (upperLimit + batch_size - 1) / batch_size;

// #define use_std_threadpool
#ifdef use_std_threadpool
					std::for_each(std::execution::par, (char*)0, (char*)(uintptr_t)batches, [&smatches, sourceArray, &opts, upperLimit, this](char& _n) {
						uint32_t n = (uint32_t)(uintptr_t)&_n;
#else
					Concurrency::parallel_for(0u, batches, 1u, [&smatches, sourceArray, &opts, upperLimit, this](uint32_t n) {
#endif
						std::basic_string<CH> temp_item_string_storage2;
						StrMatchTempStorageT<CH> temp_storage;
						for (uint32_t n2 = n * batch_size; n2 < std::min<uint32_t>((n + 1) * batch_size, upperLimit); n2++)
							smatches[n2] = ::StrMatchRankedT(ItemToText(sourceArray[n2], temp_item_string_storage2), opts, temp_storage);
					});
				}
				else
				{
					StrMatchTempStorageT<CH> temp_storage;
					for (uint32_t n = 0; n < upperLimit; n++)
						smatches[n] = ::StrMatchRankedT(ItemToText(sourceArray[n], temp_item_string_storage), opts, temp_storage);
				}
				if (opts.GetFuzzy())
					get_ltalloc()->ltsqueeze(0);

			mFilteredSetCount = 0;
				for (uint32_t n = 0; n < upperLimit; n++)
				{
					// The original loop is split in two. First part is multithreaded and this is the second part with fills the output list.
					const int8_t smatch = smatches[n];
					if (smatch)
					{
						if (matchRank < smatch)
						{
							matchRank = smatch;
							mSuggestedItemIdx = mFilteredSetCount;
						}
						mFilteredSet[mFilteredSetCount++] = sourceArray[n];
					}
				}

				if (mSuggestedItemIdx >= mFilteredSetCount)
					mSuggestedItemIdx = 0;

				// all passes after first work on current set (decision on first pass
				// is made before the outer loop)
			filterCurrentSet = true;

				if (2 == patternsGroupIdx)
				{
					// second pass of inclusivePatterns only needs to look at first one
					// (to set mSuggestedItemIdx)
					break;
				}
			}
	}
}

// used in VAOpenFile for OFIS (Open file in solution) dialog
// this is the restoration of the original method and transforming it to fit the new templated filtering system #FilterFiles
template <typename CH, typename T>
void FilterableSet<CH, T>::FilterFiles(std::basic_string_view<CH> newFilterIn, bool use_fuzzy, bool force /*= false*/, std::optional<bool> searchFullPath /*= {}*/)
{
	AutoLockCs l(mCs);
	std::basic_string<CH> newFilter(StrMatchOptionsT<CH>::TweakFilter2(std::basic_string<CH>(newFilterIn), searchFullPath.value_or(false), false));
	const std::basic_string<CH> prevFilter(mFilter);
	if (newFilter == prevFilter && mFilteredSetCount && !force)
	{
		return;
	}
	
	mSuggestedItemIdx = 0;
	mFilter = newFilter;
	bool filterCurrentSet = false;
	if (mFilteredSetCount && prevFilter.length() && prevFilter.length() <= newFilter.length() &&
	    newFilter.find(prevFilter) == 0 && !force)
	{
		filterCurrentSet = true;
		// typing a union filter can add items to result set
		if (mFilter.find(CH{','}) != std::basic_string<CH>::npos)
			filterCurrentSet = false;
		// typing a \ filter can add items to result set
		else if (mFilter.find(CH{'\\'}) != std::basic_string<CH>::npos)
		 	filterCurrentSet = false;
		// typing a negative filter can add items to result set (-c will exclude more than -cp)
		else if (mFilter.find(CH{'-'}) != std::basic_string<CH>::npos)
			filterCurrentSet = ::HaveSameExclusionFiltersT(std::basic_string_view<CH>(prevFilter), std::basic_string_view<CH>(mFilter));
	}

	auto kFilterLen = mFilter.length();
	if (kFilterLen)
	{
		StrMatchOptionsT<CH> opts(mFilter);
		if (!use_fuzzy)
			opts.mFuzzy = std::nullopt;
		const bool kIsMultiPattern = StrIsMultiMatchPatternT(mFilter);
		const int upperLimit = filterCurrentSet ? mFilteredSetCount : mAllItemCount;
		T* sourceData = filterCurrentSet ? mFilteredSet : mAllItems;
		int matchRank = -1;
		StrMatchTempStorageT<CH> temp_storage;
		for (int n = mFilteredSetCount = 0; n < upperLimit; n++)
		{
			std::basic_string<CH> storage;
			int smatch = kIsMultiPattern ? ::StrMultiMatchRankedT(ItemToText(sourceData[n], storage), mFilter, temp_storage, true, use_fuzzy)
			                             : ::StrMatchRankedT(ItemToText(sourceData[n], storage), opts, temp_storage, searchFullPath.value_or(false));
			if (smatch)
			{
				if (matchRank < smatch)
				{
					matchRank = smatch;
					mSuggestedItemIdx = mFilteredSetCount;
				}
				mFilteredSet[mFilteredSetCount++] = sourceData[n];
			}
		}
	}
	else
	{
		mFilteredSetCount = mAllItemCount;
		for (int n = 0; n < mAllItemCount; n++)
			mFilteredSet[n] = mAllItems[n];
	}
}

template FilterableSet<char, const char *>;
template FilterableSet<char, DType *>;
class FileData;
template FilterableSet<wchar_t, FileData*>;

static void GetDTypeSymTypeAndDb(DType* dtype, int& symType, int& vaDb)
{
	symType = (int)dtype->MaskedType();
	vaDb = (int)dtype->DbFlags();
}

void FilteredDTypeList::RemoveDuplicates()
{
	AutoLockCs l(mCs);
	if (!mAllItemCount)
		return;

	try
	{
		int j = 0;
		const int count = mAllItemCount;
		for (int i = 1; i < count; i++)
		{
			DType* d1 = GetOrgAt(j);
			DType* d2 = GetOrgAt(i);

			if (d1->SymScope() != d2->SymScope())
			{
				mAllItems[++j] = mAllItems[i];
			}
			else
			{
				// same sym name, check for allowable dupe
				int type1, db1;
				::GetDTypeSymTypeAndDb(d1, type1, db1);
				if (LINQ_VAR != type1)
				{
					int type2, db2;
					::GetDTypeSymTypeAndDb(d2, type2, db2);
					if (LINQ_VAR != type2)
					{
						// only allow dupes for VA_DB_Solution items due to noise in sys db
						if ((db1 & VA_DB_Solution) || (db2 & VA_DB_Solution))
						{
							if (GOTODEF == type1 || GOTODEF == type2)
							{
								// FSIS prefers definition
								if (GOTODEF == type1 && GOTODEF == type2)
								{
									if (d1->FileId() != d2->FileId() || d1->Line() != d2->Line())
									{
										// keep both GOTODEF since at different places (overloaded func)
										mAllItems[++j] = mAllItems[i];
									}
								}
								else if (GOTODEF == type1)
								{
									// keep the first (GOTODEF), skip the second (FUNC?)
								}
								else if (GOTODEF == type2)
								{
									// overwrite the first (FUNC?) with the second (GOTODEF)
									mAllItems[j] = mAllItems[i];
								}
							}
							else if (type1 == type2)
							{
								// same name and type
								if (CLASS == type1 || STRUCT == type1 || C_INTERFACE == type1)
								{
									if (d1->FileId() == d2->FileId() && d1->Line() == d2->Line())
									{
										// [case: 109339]
										// disallow identical items
									}
									else
									{
										// [case: 54823] allow class/struct dupes
										mAllItems[++j] = mAllItems[i];
									}
								}
								else if (FUNC == type1)
								{
									if (d1->FileId() != d2->FileId() || d1->Line() != d2->Line())
									{
										// check def
										bool isGotoDef1 = d1->Def().Find("{...}") != -1;
										bool isGotoDef2 = d2->Def().Find("{...}") != -1;
										if (isGotoDef1 && isGotoDef2)
										{
											// keep both GOTODEF since at different places (overloaded func)
											mAllItems[++j] = mAllItems[i];
										}
										else if (isGotoDef1)
										{
											// keep the first (GOTODEF), skip the second (FUNC)
										}
										else if (isGotoDef2)
										{
											// overwrite the first (FUNC) with the second (GOTODEF)
											mAllItems[j] = mAllItems[i];
										}
										else
										{
											// keep both FUNC since at different places (overloaded func)
											mAllItems[++j] = mAllItems[i];
										}
									}
								}
								// #ifdef _DEBUG
								//								else if (NAMESPACE != type1 &&
								//									C_ENUM != type1 &&
								//									VAR != type1 &&
								//									TEMPLATETYPE != type1 &&
								//									DEFINE != type1 &&
								//									EVENT != type1 &&
								//									PROPERTY != type1)
								//								{
								//									// what other things show up with duplicate types?
								//									_asm nop;
								//								}
								// #endif // _DEBUG
							}
							else
							{
								// duplicate name but different types (other than GOTODEF and its match)?
								// TYPE/FUNC, FUNC/VAR, CLASS/NAMESPACE
								// INTERFACE/STRUCT/TYPE - important for COM interfaces that have 3 decls
								if (d1->FileId() != d2->FileId() || d1->Line() != d2->Line())
									mAllItems[++j] = mAllItems[i];
							}
						}
					}
				}
			}
		}

		_ASSERTE(j + 1 <= mCapacity);
		mAllItemCount = j + 1;
	}
	catch (...)
	{
		VALOGEXCEPTION("FilteredStringList::RemoveDupes:Err");
	}
}

std::string_view FilteredDTypeList::ItemToText(DType* item, std::string &storage) const
{
	GetSymScopeOperatorSafe_dump(item, storage);
	return storage;
}

bool SortDTypesBySymScope(DType*& d1, DType*& d2)
{
	// sort by symscope, then file, then line
	WTString symscope1 = d1->SymScope();
	WTString symscope2 = d2->SymScope();
	if (symscope1 == symscope2)
	{
		if (d1->FileId() == d2->FileId())
			return d1->Line() < d2->Line();
		return d1->FilePath().CompareNoCase(d2->FilePath()) < 0;
	}
	return symscope1.Compare(symscope2.c_str()) < 0;
}

template <bool sortByType>
bool SortDTypesBySym(DType*& d1, DType*& d2)
{
	if (sortByType)
	{
		uint t1 = d1->MaskedType();
		uint t2 = d2->MaskedType();

		// not exactly right.  some member vars showing up as GOTODEF too
		if (t1 == GOTODEF)
			t1 = FUNC;
		if (t2 == GOTODEF)
			t2 = FUNC;

		if (t1 != t2)
		{
			// need better sorting?
			return int(t1) < int(t2);
		}
	}

	// sort by sym, then scope, then file, then line
	WTString sym1 = GetSymOperatorSafe(d1);
	WTString sym2 = GetSymOperatorSafe(d2);
	if (sym1 == sym2)
	{
		WTString scope1 = d1->Scope();
		WTString scope2 = d2->Scope();
		if (scope1 == scope2)
		{
			if (d1->FileId() == d2->FileId())
				return d1->Line() < d2->Line();
			return d1->FilePath().CompareNoCase(d2->FilePath()) < 0;
		}
		return scope1.CompareNoCase(scope2.c_str()) < 0;
	}
	else if (sym1.CompareNoCase(sym2.c_str()) == 0)
	{
		return sym1.Compare(sym2.c_str()) < 0;
	}
	return sym1.CompareNoCase(sym2.c_str()) < 0;
}

void FilteredDTypeList::PreSort()
{
	Sort(::SortDTypesBySymScope);
}

void FilteredDTypeList::PostSort(bool byType)
{
	if (byType)
		Sort(::SortDTypesBySym<true>);
	else
		Sort(::SortDTypesBySym<false>);
}
