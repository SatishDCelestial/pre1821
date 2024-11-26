#include "stdafxed.h"
#include "WTHashList.h"
#include "myspell.hxx"
#include "../Settings.h"
#include "../WTString.h"
#include "..\Directories.h"
#include "..\file.h"
#include "StringUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

const int kMaxCheckers = 4;
static MySpell* pCheckers[kMaxCheckers];
static int nCheckers = 0;

static WTHashList sVaWordList;
static BOOL s_Initialized = FALSE;
static CStringW sUserwordsFilename;

static int GetHash(LPCSTR str)
{
	int hash = 0;
	while (*str)
		hash += (*(str++)) & 0x7f;
	return hash;
}

static void InitDictionary();

void LoadUserList(const CStringW& inFile)
{
	sUserwordsFilename = inFile;
	FILE* fp = _wfopen(sUserwordsFilename, L"r");
	if (!fp)
		return;

#define WTMAXWORDLEN 1024
	const std::unique_ptr<char[]> buf(new char[WTMAXWORDLEN + 1]);
	for (;;)
	{
		int n = fscanf(fp, "%s\n", buf.get());
		if (n <= 0)
			break;

		if (!::CanReadAsUtf8(buf.get()))
		{
			// can't convert from mbcs codepage to utf8, so convert to wide then narrow to utf8
			const int k = strlen_i(buf.get());
			WTString tmpwt(::MbcsToWide(buf.get(), k, (int)::GetACP()));
			// and copy back to local buf
			if (tmpwt.GetLength() < WTMAXWORDLEN)
				::strncpy_s(buf.get(), WTMAXWORDLEN, tmpwt.c_str(), (size_t)tmpwt.GetLength());
		}

		FPSAddWord(buf.get(), true);
	}
	fclose(fp);
}

WTHashList::WTHashList()
{
	ZeroMemory(pRows, sizeof(pRows));
	nEntries = 0;
}

WTHashList::~WTHashList()
{
	Flush();
	for (int i = 0; i < nCheckers; i++)
	{
		delete pCheckers[i];
		pCheckers[i] = NULL;
	}
	nCheckers = 0;
}

void WTHashList::Flush()
{
	if (!nEntries)
		return;
	for (int i = 0; i < LISTSZ; i++)
	{
		_Item* it = pRows[i];
		while (it)
		{
			_Item* lit = it;
			it = it->GetNext();
			delete lit;
		}
		pRows[i] = NULL;
	}
	nEntries = 0;
}

_Item* WTHashList::Add(LPCSTR str, int flag /* = 0 */)
{
	if (!s_Initialized)
		InitDictionary();
	int hv = GetHash(str) % LISTSZ;
	_Item* row = pRows[hv];

	while (row && row->GetNext())
	{
		LPSTR wd = row->GetStr();
		if (strcmp(wd, str) == 0)
			return row;
		row = row->GetNext();
	}
	nEntries++;
	_Item* nItem = _Item::create(str);
	if (row)
		row->SetNext(nItem);
	else
		pRows[hv] = nItem;

	//	LPCSTR apostpos = strchr(str, '\'');
	try
	{
		int found = FALSE;
		for (int i = 0; !found && i < nCheckers; i++)
		{
			found = pCheckers[i]->spell(str);
		}
		if (!found)
			nItem->SetFlag(0x1);
	}
	catch (...)
	{
		// just in case FPSSpell bombs
	}
	//	SendIpcMessage("SpellWord", nItem, sizeof(_Item));
	return nItem;
}

_Item* WTHashList::Find(LPCSTR str)
{
	if (!str)
		return NULL;
	if (!s_Initialized)
		::InitDictionary();
	int hv = ::GetHash(str) % LISTSZ;
	for (int idx = 2; idx; idx--)
	{
		_Item* row = pRows[hv];
		while (row)
		{
			LPCSTR wd = row->GetStr();
			if (_tcsicmp(str, wd) == 0)
				return row;
			row = row->GetNext();
		}

		if (idx == 1)
			break;

		WTString secondTry(str);
		secondTry.MakeLower();
		if (!(str[0] >= 'A' && str[0] <= 'Z'))
			secondTry.SetAt(0, (char)::toupper(secondTry[0]));
		hv = ::GetHash(secondTry.c_str()) % LISTSZ;
	}

	return NULL;
}

void WTHashList::Remove(LPCSTR str)
{
	int hv = GetHash(str) % LISTSZ;
	_Item* row = pRows[hv];
	_Item* lrow = NULL;
	while (row)
	{
		LPSTR wd = row->GetStr();
		if (strcmp(str, wd) == 0)
		{
			if (lrow)
				lrow->SetNext(row->GetNext());
			else
				pRows[hv] = NULL;
			nEntries--;
			delete row;
			return;
		}
		lrow = row;
		row = row->GetNext();
	}
}

BOOL FPSSpell(LPCSTR str, CStringList* plst)
{
	if (plst)
		plst->RemoveAll();

	if (!str || !str[0] || (strlen(str) > 64))
		return TRUE;

	if (!s_Initialized)
		InitDictionary();

	if (plst)
	{
		char** wlst = NULL;
		int ns = 0;
		for (int n = 0; n < nCheckers; n++)
		{
			ns = pCheckers[n]->suggest(&wlst, str);
			for (int i = 0; i < ns; i++)
			{
				if (!(strstr(wlst[i], "fuck") && strstr(wlst[i], "shit") && strstr(wlst[i], "damn")))
				{
					// check for duplicates if there's more than one dictionary
					if (!nCheckers || !plst->Find(wlst[i]))
					{
						if (n)
						{
							// mix suggestions in with previous language
							POSITION p = plst->FindIndex(2 * i);
							if (p)
								plst->InsertAfter(p, wlst[i]);
							else
								plst->AddTail(wlst[i]);
						}
						else
							plst->AddTail(wlst[i]);
					}
				}
				if (wlst[i] != NULL)
					free(wlst[i]);
			}
			if (wlst)
				delete wlst;
		}
		return TRUE;
	}

	if (sVaWordList.Find(str) != NULL)
		return TRUE;

	for (int i = 0; i < nCheckers; i++)
	{
		if (pCheckers[i]->spell(str))
			return TRUE;
	}

	if (strchr(str, '_') != NULL || strchr(str, '$') != NULL)
		return TRUE;

	if ((str[0] & 0xE0) == 0xE0)
	{
		// [case: 132797]
		// start of 3-byte or 4-byte sequence
		// don't underline strings that start with CJK/Asian/Hangul characters
		// https://en.wikipedia.org/wiki/UTF-8
		return TRUE;
	}

	return FALSE;
}

// this function will try to obtain the word before or after the current word if they are separated with ampersand
// if obtained, they will be together spell checked
// if true is returned, original cwd should be still considered as an error
bool FPSSpellSurroundingAmpersandWord(const char* stream, int start_index, int end_index, const WTString& cwd)
{
	bool amp_suffix = stream[end_index] == '&';
	bool amp_prefix = (start_index > 0) && (stream[start_index - 1] == '&');
	const int max_other_word_len = 20;
	WTString new_cwd = cwd;
	if (amp_suffix && !amp_prefix)
	{
		for (int k = 1; (k <= max_other_word_len) && stream[k + end_index] &&
		                !strchr("\r\n\"'", stream[k + end_index]) && ISCSYM(stream[k + end_index]);
		     k++)
			new_cwd.append(stream[k + end_index]);
	}
	else if (amp_prefix && !amp_suffix)
	{
		for (int k = 1; (k <= max_other_word_len) && (start_index > k) && stream[start_index - 1 - k] &&
		                !strchr("\r\n\"'", stream[start_index - 1 - k]) && ISCSYM(stream[start_index - 1 - k]);
		     k++)
			new_cwd = stream[start_index - 1 - k] + new_cwd;
	}

	return (new_cwd.GetLength() == cwd.GetLength()) || !FPSSpell(new_cwd.c_str(), NULL);
}

void FPSAddWord(LPCSTR text, BOOL ignore)
{
	if (!s_Initialized)
		InitDictionary();

	for (int i = 0; i < nCheckers; i++)
		pCheckers[i]->AddWord(text);

	if (ignore)
		return;

	FILE* fp = _wfopen(sUserwordsFilename, L"a");
	if (fp)
	{
		fputs(text, fp);
		fputs("\n", fp);
		fclose(fp);
	}
}

// add common words used in development that aren't standard dictionary words
static void InitVaList()
{
	const char* words[] = {"accessor",
	                       "alloc",
	                       "ansi",
	                       "app",
	                       "apps",
	                       "ascii",
	                       "arduino",
	                       "Atmel",
	                       "atmega",
	                       "avr",
	                       "basename",
	                       "baz",
	                       "bitfield",
	                       "clr",
	                       "coclass",
	                       "combobox",
	                       "config",
	                       "const",
	                       "cpp"
	                       "css",
	                       "ctor",
	                       "ctrl",
	                       "debuggee",
	                       "dlg",
	                       "dll",
	                       "dockable",
	                       "doxygen",
	                       "dropdown",
	                       "dtor",
	                       "ecmascript",
	                       "elif",
	                       "endian",
	                       "endif",
	                       "endregion",
	                       "filepath",
	                       "fn",
	                       "foo",
	                       "framerate",
	                       "github",
	                       "guids",
	                       "hsync",
	                       "idx",
	                       "ifdef",
	                       "ifndef",
	                       "imagelist",
	                       "inline",
	                       "interop",
	                       "lifecycle",
	                       "linebreak",
	                       "listbox",
	                       "javascript",
	                       "jira",
	                       "jscript",
	                       "metadata",
	                       "mipmap",
	                       "modeless",
	                       "multiline",
	                       "namespace",
	                       "nodiscard",
	                       "okay",
	                       "overridables",
	                       "param",
	                       "params",
	                       "paren",
	                       "parens",
	                       "plugin",
	                       "pragma",
	                       "prog",
	                       "progid",
	                       "rects",
	                       "refactor",
	                       "refactors",
	                       "refactoring",
	                       "renders",
	                       "renderers",
	                       "screenshot",
	                       "segoe",
	                       "shader",
	                       "shaders",
	                       "skipall",
	                       "slp",
	                       "stylesheet",
	                       "subitem",
	                       "timestep",
	                       "timestamp",
	                       "tmp",
	                       "todo",
	                       "toolbar",
	                       "tooltip",
	                       "tooltips",
	                       "typelib",
	                       "ui",
	                       "undef",
	                       "unhandled",
	                       "uninstall",
	                       "unmap",
	                       "unregserver",
	                       "urls",
	                       "username",
	                       "var",
	                       "varchar",
	                       "variadic",
	                       "vbscript",
	                       "vertices",
	                       "viewport",
	                       "vsync",
	                       "wordbreak",
	                       "workarea",
	                       "workareas",
	                       "xml",
	                       NULL};

	for (int idx = 0; words[idx]; ++idx)
		sVaWordList.Add(words[idx], 0x8);
}

static void LoadDictionaryDirectory(const CStringW& dir)
{
	// Get dictionaries
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	hFile = FindFirstFileW(dir + L"*.aff", &fileData);
	if (hFile != INVALID_HANDLE_VALUE && nCheckers < kMaxCheckers)
	{
		do
		{
			const CStringW afffile = dir + fileData.cFileName;
			const CStringW dicfile = afffile.Mid(0, afffile.GetLength() - 4) + L".dic";
			pCheckers[nCheckers++] = new MySpell(afffile, dicfile);
		} while (FindNextFileW(hFile, &fileData) && nCheckers < kMaxCheckers);

		FindClose(hFile);
	}
}

// NOTE:  You can get more dictionaries at http://lingucomponent.openoffice.org/spell_dic.html

void InitDictionary()
{
	s_Initialized = TRUE;

	InitVaList();

	const CStringW kAppDictDir(VaDirs::GetDllDir() + L"Dict\\");
	LoadDictionaryDirectory(kAppDictDir);

	const CStringW kUserDictDir(VaDirs::GetUserDir() + L"Dict\\");
	LoadDictionaryDirectory(kUserDictDir);

	LoadUserList(kUserDictDir + L"UserWords.txt");

	if (!IsFile(kUserDictDir + L"UserWords.txt") && IsFile(kAppDictDir + L"..\\USUser.Dic"))
	{
		// import old user keywords.
#if !defined(SEAN)
		try
#endif // !SEAN
		{
			CFileW oldFile;
			oldFile.Open(kAppDictDir + L"..\\USUser.Dic", CFile::typeBinary);
			ULONGLONG fs = oldFile.GetLength();
			if (fs && fs != -1)
			{
				const int dataOffset = 392; // hard coded values for data sizes in the old FPSSpell data structures
				const int dataLen = 52;
				char* buf = new char[(UINT)fs + 1];
				oldFile.Read(buf, (UINT)fs);

				for (DWORD i = dataOffset; i < (DWORD)fs; i += dataLen)
				{
					if (!buf[i - 1] && ISCSYM(buf[i]))
						FPSAddWord(&buf[i], FALSE);
				}
				delete[] buf;
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("WTHL:");
		}
#endif // !SEAN
	}

#ifdef AVR_STUDIO
	// add "Atmel" and "AVR" to existing UserWords.txt: per request from Atmel, Jo Inge
	if (!FPSSpell("Atmel", NULL))
		FPSAddWord("Atmel", FALSE);
	if (!FPSSpell("Avr", NULL))
		FPSAddWord("Avr", FALSE);
#endif // AVR_STUDIO
}
