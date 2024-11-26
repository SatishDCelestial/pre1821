#pragma once

#include "OWLDefs.h"
#include "WTString.h"
#include "parse.h"
#include "wtcsym.h"
#include "Foo.h"
#include <set>
#include <map>
#include <memory>
#include <utility>
#include "DBFile\DbTypeId.h"
#include "DTypeDbScope.h"
#include "FileList.h"
#include "DbFieldConstants.h"

class DBFilePair;
class EdCnt;
class CCodeBuf;
class CMacro;
class FindReferences;
class LineMarkers;
class Dic;
class FileDic;

#define MAXLN 4096

enum DictionaryType
{
	DLoc,
	DMain,
	DSys
};

enum ParseType
{
	ParseType_NotSet = 0,
	ParseType_Globals,
	ParseType_Locals,
	ParseType_GotoDefs
};

// this is just for temporary storage of cached data where DType is not appropriate
// (DTypes can read from datafiles, this struct is used to populate the datafiles).
struct DbOutParseData
{
	WTString mSym;
	WTString mDef;
	uint mType;
	uint mAttrs;
	int mLine;
	DWORD mFileId;
	uint mDbFlags; // optional, normally unset

	DbOutParseData(const WTString& sym, const WTString& def, uint type, uint attrs, int line, DWORD fileID,
	               uint dbFlags = 0)
	    : mSym(sym), mDef(def), mType(type), mAttrs(attrs), mLine(line), mFileId(fileID), mDbFlags(dbFlags)
	{
	}
};

using DbOutData = std::vector<DbOutParseData>;
using DicPtr = std::shared_ptr<Dic>;
using FileDicPtr = std::shared_ptr<FileDic>;
using DTypeForEachCallback = std::function<void(DType*, bool)>;

class MultiParse;
using MultiParsePtr = std::shared_ptr<MultiParse>;

#include "ScopeInfo.h"
class MultiParse : public ScopeInfo, public std::enable_shared_from_this<MultiParse>
{
#if defined(VA_CPPUNIT)
	friend class ColorLookup_test;
#endif
  public:
	static MultiParsePtr Create(int ftype = 0, int hashRows = -1)
	{
		return std::make_shared<MultiParse>(ftype, hashRows);
	}
	static MultiParsePtr Create(MultiParsePtr fp, int ftype = 0, int hashRows = -1)
	{
		return std::make_shared<MultiParse>(fp, ftype, hashRows);
	}

	// I wanted these ctors to be private, but then std::make_shared can't create instances of the class
	MultiParse(int ftype = 0, int hashRows = -1);
	MultiParse(MultiParsePtr fp, int ftype = 0, int hashRows = -1);
	~MultiParse();

	ScopeInfo& GetScopeInfo()
	{
		return *this;
	}
	DType* FindSym(const WTString* sym, const WTString* scope, const WTString* baseclasslist, int findFlag = FDF_NONE);
	DicPtr LDictionary()
	{
		return m_pLocalDic;
	}
	FileDic* SDictionary()
	{
		return SDictionary(m_ftype);
	}
	static FileDic* SDictionary(int ftype);
	FileDicPtr LocalHcbDictionary()
	{
		return m_pLocalHcbDic;
	}

	void Stop();
	bool IsStopped() const
	{
		return m_stop;
	}

	void AddPPLn();
	void AddPPLnMacro();
	void CwScopeInfo(WTString& scope, WTString& baseclasslist, int devlang);
	void CwScope(WTString& scope, WTString& sym, WTString& def, uint& type, uint& attrs, uint& dbFlags);
	void FormatFile(const CStringW ifile, uint attr, ParseType parseType, bool isReparse = false, LPCSTR text = NULL,
	                uint dbFlags = 0);
	void SpellBlock(EdCnt* ed, long from, long to);
	void add(const WTString& s, const WTString& d, DictionaryType dict = DMain, uint type = VAR, uint attrs = 0,
	         uint dbFlags = 0, uint fileId = 0, int line = 0);
	void ReadDBFile(const CStringW& dbFile);
	void ProcessPendingIncludes(uint dbFlags);
	int ReadDFile(const CStringW& file, uint addlAttrs, uint addlDbFlags = 0, char sep = DB_FIELD_DELIMITER);
	void AddResword(const WTString& sym);
	static bool IsIncluded(const CStringW& file, BOOL checkDate = FALSE);
	bool ProcessIncludeLn(const WTString& ln, uint itype, uint attrs = 0);
	WTString GetBaseClassList(DType* pDat, const WTString& bc, bool force = false,
	                          volatile const INT* bailMonitor = NULL, int scopeLang = 0);
	WTString GetBaseClassList(const WTString& bc, bool force = false, volatile const INT* bailMonitor = NULL,
	                          int scopeLang = 0)
	{
		return GetBaseClassList(nullptr, bc, force, bailMonitor, scopeLang);
	}
	WTString GetBaseClassListCached(const WTString& bc);
	WTString GetGuessedBaseClass(const WTString& bc);
	DType* GuessAtBaseClass(WTString& sym, const WTString& bc);
	DTypePtr GetXrefCwData() const
	{
		return mUpdatedXref ? mUpdatedXref : GetCwData();
	}
	void Tdef(const WTString& s, WTString& def, int AllDefs, int& type, int& attrs);
	WTString def(const WTString& sym);
	bool IsPointer(const WTString& symdef, int deep = 0);
	bool HasPointerOverride(const WTString& sym);
	void ParseFileForGoToDef(const CStringW& file, bool sysFile, LPCSTR text = NULL, int extraDbFlags = 0);
	DType* GetMacro(const WTString& macro);  // searches only :1 scope
	DType* GetMacro2(const WTString& macro); // searches both :1 and :2 scope
	static DTypePtr GetFileData(const CStringW& fileAndPath);
	void SetBuf(LPCSTR buf, ULONG bufLen, ULONG cp, ULONG curLine);
	void ClearBuf();
	void ClearCwData();
	void SetCacheable(BOOL cacheable)
	{
		m_cacheable = cacheable;
	}
	BOOL GetCacheable() const
	{
		return m_cacheable;
	}

	void ForEach(DTypeForEachCallback fn, bool& stop, bool searchSys = true);
	void ForEach(DTypeForEachCallback fn, bool& stop, DTypeDbScope dbScope);

	int FindExactList(LPCSTR sym, DTypeList& cdlist, bool searchSys = true, bool searchProj = true);
	int FindExactList(const WTString& sym, DTypeList& cdlist, bool searchSys = true, bool searchProj = true)
	{
		return FindExactList(sym.c_str(), cdlist, searchSys, searchProj);
	}
	int FindExactList(uint hashVal, uint scopeId, DTypeList& cdlist, bool searchSys = true, bool searchProj = true);
	DType* FindExact(LPCSTR sym);
	DType* FindExact(const WTString& sym)
	{
		return FindExact(sym.c_str());
	}
	// FindExact2 can return multiple concat'd defs in a single DType (use FindExactList if that is a problem)
	DType* FindExact2(LPCSTR sym, bool concatDefs = true, int searchForType = 0, bool honorHideAttr = true);
	DType* FindExact2(const WTString& sym, bool concatDefs = true, int searchForType = 0,
	                  bool honorHideAttr = true)
	{
		return FindExact2(sym.c_str(), concatDefs, searchForType, honorHideAttr);
	}
	DType* FindCachedBcl(const WTString& sym);
	// return any DType from any scope whose sym matches str
	DType* FindAnySym(const WTString& str);
	DTypePtr FindBestColorType(const WTString& str);
	DType* FindOpData(WTString opStr, WTString key, volatile const INT* bailMonitor);
	void FindAllReferences(const CStringW& project, const CStringW& file, FindReferences* ref, WTString *txt = NULL,
	                       volatile const INT* monitorForQuit = NULL, bool fullscope = true);
	DType* FindMatch(DTypePtr symDat);
	// cached DBOut
	void DBOut(const WTString& sym, const WTString& def, uint type, uint attrs, int line);
	// non-cached DBOut
	void ImmediateDBOut(const WTString& sym, const WTString& def, uint type, uint attrs, int line);

	void RemoveAllDefs(const CStringW& file, DTypeDbScope dbScp, bool singlePass = true);
	void RemoveAllDefs(const std::set<UINT>& fileIds, DTypeDbScope dbScp);
	void RemoveMarkedDefs(DTypeDbScope dbScp);

	static void ResetDefaultNamespaces();
	BOOL mIsVaReservedWordFile = FALSE;

  private:
	void Init();
	WTString SubStr(int p1, int p2);
	bool ReadTo(const char* tostr, uint colorType, uint attrs = 0);
	void ParseFile(const CStringW& ifile, bool parseall, WTString& initscope, LPCSTR text = NULL, uint dbFlags = 0);
	// returns last scope of file
	WTString ParseStr(const WTString& code, const WTString& InitScope);
	int ParseBlockHTML();
	int qParseBlock();
	void ReadIdxFile();
	int ReadIdxData(LPCVOID data, DWORD sz, BOOL checkIncludes);
	const char* FormatDef(int type = 0);
	void AddMacro(LPCSTR macro, LPCSTR macrotxt, BOOL forExpandAllOnly = FALSE);
	void AddStuff(const WTString& sym, WTString& def, uint itype, uint attrs, uint dbFlags);
	void AddDef(const WTString& sym, WTString& def, uint itype, uint attrs, uint dbFlags);
	void DoFormatFile(const CStringW& ifile, uint attr, ParseType parseType, bool isReparse, LPCSTR text, uint dbFlags);

	int m_cp; // cur pos
	const char* m_p;
	long mBufSetCnt = 0;
	CCriticalSection mBufLock;
	DicPtr m_pLocalDic;
	FileDicPtr m_pLocalHcbDic;
	int m_len;
	bool m_stop = false;
	DbTypeId m_VADbId = DbTypeId_Error;
	DBFilePair* m_pDBPair = nullptr;
	void OpenIdxOut(const CStringW& file, uint dbFlags);
	void FlushDbOutCache();
	void CloseIdxOut();
	void ClearTemporaryMacroDefs();
	MultiParsePtr m_mp; // parent
	char* m_pDefBuf = nullptr;
	std::vector<char> m_DefBuf;
	EdCnt* m_ed = nullptr; // this is a temporary used during SpellCheck
	int m_spellFromPos;
	enum
	{
		HTML_inScript,
		HTML_inTag,
		HTML_inText,
		HTML_Count
	} m_HTML_Scope = HTML_Count;
	BOOL m_cacheable = FALSE;
	WTString m_namespaces;
	static WTString s_SrcNamespaces;
	static WTString s_SrcNamespaceHints;
	DTypePtr mUpdatedXref;
	uint mSolutionHash = 0;

  public:
	enum
	{
		DB_VA = 0x1,
		DB_LOCAL = 0x2,
		DB_GLOBAL = 0x4,
		DB_SYSTEM = 0x8,
		DB_ALL = 0xff
	};

	WTString GetGlobalNameSpaceString(bool includeHints = true);
	void SetGlobalNameSpaceString(const WTString& namespaces);
	void AddGlobalNamespaceHint(const WTString& namespaces);
	void AddNamespaceString(WTString scope, WTString ns);
	WTString GetNameSpaceString(WTString scopeContext) const;

  private:
	WTString DoGetNameSpaceString(WTString scopeContext) const;
	DType FindBetterDtype(DType* d);
	using NsSet = phmap::flat_hash_set<WTString>;
	using NsSetPtr = std::unique_ptr<NsSet>;
	using NsMap = phmap::flat_hash_map<WTString, NsSetPtr>;
	static NsMap s_scopedNamespaceMap;

	DbOutData mDbOutCache, mDbOutIncludesCache, mDbOutIncludeByCache, mDbOutInheritsFromCache;
	int mDbOutMacroLocalCnt = 0;
	int mDbOutMacroSysCnt = 0;
	int mDbOutMacroProjCnt = 0;
	CCriticalSection mPendingParseLock;
	FileList mPendingIncludes;
#if defined(VA_CPPUNIT)
	friend class Mparse_test;
#endif
};

// Generic class to iterate through matching symbols in each dictionary
class VAHashTable;
class DefObj;

class DB_Iterator
{
  protected:
	int m_DB_ID;
	uint m_hashID;
	MultiParsePtr m_mp;
	WTString m_sym;
	BOOL m_break;

  public:
	virtual ~DB_Iterator() = default;

	void Iterate(MultiParsePtr mp, const WTString& sym);
	void IterateDB(VAHashTable* db);
	void Break()
	{
		m_break = TRUE;
	}
	virtual void OnSym(DefObj* obj)
	{
	}
	virtual void OnNextDB(int id)
	{
	}
};

WTString StrGetSymScope(LPCSTR symscope); // given ":foo:bar:baz" returns ":foo:bar"
std::string_view StrGetSymScope_sv(LPCSTR symscope);
std::string_view StrGetSymScope_sv(std::string_view symscope);
inline WTString StrGetSymScope(const WTString& symscope)
{
	return StrGetSymScope(symscope.c_str());
} // given ":foo:bar:baz" returns ":foo:bar"
WTString StrGetOuterScope(const WTString& symscope); // given ":foo:bar:baz" returns ":foo"
LPCSTR StrGetSym(LPCSTR symscope);                   // given ":foo:bar:baz" returns "baz"
inline LPCSTR StrGetSym(const WTString& symscope)
{
	return StrGetSym(symscope.c_str());
} // given ":foo:bar:baz" returns "baz"
std::string_view StrGetSym_sv(std::string_view symscope);
BOOL IsReservedWord(std::string_view sym, int devLang, bool typesShouldBeConsideredReserved = true);
WTString GetTypesFromDef(LPCSTR symScope, LPCSTR symDef, int maskedType, int devLang);
inline WTString GetTypesFromDef(const WTString& symScope, const WTString& symDef, int maskedType, int devLang)
{
	return GetTypesFromDef(symScope.c_str(), symDef.c_str(), maskedType, devLang);
}
WTString GetTypesFromDef(const DType* dt, int maskedType, int devLang);
WTString GetTypeFromDef(LPCSTR def, int devLang);
inline WTString GetTypeFromDef(const WTString& def, int devLang)
{
	return GetTypeFromDef(def.c_str(), devLang);
}
WTString GetDefsFromMacro(const WTString& sScope, const WTString& sDef, int langType, MultiParse* mp);
WTString StripEncodedTemplates(LPCSTR symscope);
inline WTString StripEncodedTemplates(const WTString& symscope)
{
	return StripEncodedTemplates(symscope.c_str());
}
void CleanupTypeString(WTString& typeStr, int fType);

char EncodeChar(TCHAR c);
char DecodeChar(TCHAR c);
WCHAR DecodeCharW(WCHAR c);
WTString EncodeScope(LPCSTR scope);
inline WTString EncodeScope(const WTString& scope)
{
	return EncodeScope(scope.c_str());
}
void EncodeScopeInPlace(WTString &scope);
WTString DecodeScope(LPCSTR scope);
inline WTString DecodeScope(const WTString& scope)
{
	return DecodeScope(scope.c_str());
}
CStringW DecodeScope(const CStringW& scope);
void DecodeScopeInPlace(WTString& scope);
void EncodeTemplates(WTString& encStr);
extern volatile int gTypingDevLang;
WTString DecodeTemplates(const WTString& str, int devLang = gTypingDevLang);
void DecodeTemplatesInPlace(WTString &str, int devLang = gTypingDevLang);

WTString CleanScopeForDisplay(LPCSTR scope);
inline WTString CleanScopeForDisplay(const WTString& scope)
{
	return CleanScopeForDisplay(scope.c_str());
}
CStringW CleanScopeForDisplayW(LPCWSTR scope);
WTString CleanDefForDisplay(LPCSTR scope, int devLang);
inline WTString CleanDefForDisplay(const WTString& scope, int devLang)
{
	return CleanDefForDisplay(scope.c_str(), devLang);
}
void ScopeToSymbolScope(WTString& scope);
MultiParsePtr GetDFileMP(int ftype);
void FreeDFileMPs();
void ReloadDFileMP(int ftype);
void AddSpecialCppMacros();
void AddSpecialCsMacros();
void ParseGlob(LPVOID file);
void LoadMiscFileTypes(int dbID, int loadFType);
void LoadInstallDirMiscFiles(int ftype);
void ClearMpStatics();
CStringW GetIncFileStr(LPCSTR incln, BOOL& isSystem);
int GetTypeOfUnrealAttribute(LPCSTR txt);
int GetTypeOfUnrealAttribute(LPCWSTR txt);

#define WILD_CARD "wILDCard"
#define WILD_CARD_SCOPE ":wILDCard\f"
extern const WTString kWildCardScopeStr;

// utilities
WTString RemoveComments(const WTString& str, int devLang = Src);
WTString RemoveCommentsAndSpaces(const WTString& str, int devLang = Src);

#pragma warning(disable : 4365)
template <char opening, char closing, typename IT>
inline IT FindMatchingBrace(IT begin, const IT end, size_t starting = 0)
{
	static_assert(opening != closing);
	assert(!starting || (starting <= (size_t)std::distance(begin, end)));
	if (starting && (starting > (size_t)std::distance(begin, end)))
		return end;
	begin += starting;
	assert(begin != end);
	if (begin == end)
		return end;
	assert(*begin == opening);
	if (*begin != opening)
		return end;

	int counter = 0;
	for (; begin != end; ++begin)
	{

		if (*begin == opening)
			++counter;
		else if (*begin == closing)
		{
			if (--counter == 0)
				return begin;
		}
	}
	return end;
}
#pragma warning(default : 4365)

#include <iterator>
template <typename IT>
class fold_braces_iterator
{
  public:
	using me_t = fold_braces_iterator<IT>;
	using ch_t = std::iterator_traits<IT>::value_type;
	static_assert(std::is_same_v<ch_t, char>, "IT should dereference to char!");

	using difference_type = void;
	using value_type = ch_t;
	using pointer = void;
	using reference = void;
	using iterator_category = std::input_iterator_tag;

	fold_braces_iterator(IT begin_it, IT end_it)
	    : begin_it(begin_it), end_it(end_it)
	{
	}
	fold_braces_iterator(const me_t& other)
	    : begin_it(other.begin_it), end_it(other.end_it)
	{
	}

	bool operator!() const
	{
		return begin_it == end_it;
	}
	// 	operator bool() const
	// 	{
	// 		return !!*this;
	// 	}
	bool operator==(const me_t& other) const
	{
		return begin_it == other.begin_it;
	}
	bool operator!=(const me_t& other) const
	{
		return !(*this == other);
	}
	char operator*() const
	{
		assert(begin_it != end_it);
		return *begin_it;
	}
	me_t& operator++()
	{
		assert(begin_it != end_it);
		++begin_it;

		while (begin_it != end_it)
		{
			std::optional<IT> closing;
			switch (*begin_it)
			{
			case '(':
				closing = FindMatchingBrace<'(', ')'>(begin_it, end_it);
				break;
			case '{':
				closing = FindMatchingBrace<'{', '}'>(begin_it, end_it);
				break;
			case '[':
				closing = FindMatchingBrace<'[', ']'>(begin_it, end_it);
				break;
			case '<':
				closing = FindMatchingBrace<'<', '>'>(begin_it, end_it);
				break;
			}
			if (closing && (*closing != end_it))
				begin_it = ++*closing;
			else
				break;
		}

		return *this;
	}

  protected:
	IT begin_it;
	const IT end_it;
};
template <typename IT>
inline auto fold_braces(IT begin, IT end)
{
	return std::make_pair(fold_braces_iterator(begin, end), fold_braces_iterator(end, end));
}
// it will return a string with removed contents within (), [], {} and <> braces
inline std::string fold_braces_s(std::string_view sv)
{
	return {fold_braces_iterator(sv.begin(), sv.end()), fold_braces_iterator(sv.end(), sv.end())};
}
