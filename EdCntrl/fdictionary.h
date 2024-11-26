#pragma once

#include "DBFile/VADBFile.h"
#include <set>
#include "SpinCriticalSection.h"

// forward hashing from string.h

constexpr inline UINT WTHashKey(std::string_view sv)
{
	UINT nHash = 0;
	for (char ch : sv)
		nHashAdd(ch);
	return nHash;
}
UINT WTHashKeyW(LPCWSTR key);
UINT WTHashKeyNoCaseW(LPCWSTR key); // NOTE: WTHashKeyW("xxx") != WTHashKeyNoCaseW("XXX") !!!!

class ExpansionData;
class VAHashTable;
class DType;
class DTypeList;
class FindData;
class DType;
class SymbolPosList;

using DTypeForEachCallback = std::function<void(DType*, bool)>;

class Dic
{
  private:
	CCriticalSection m_lock;
	VAHashTable* mHashTable;

  public:
	CCriticalSection* GetDBLock()
	{
		return &m_lock;
	}
	Dic(uint rows);
	~Dic();
	void add(const WTString& s, const WTString& d, uint symType = 0, uint symAttrs = 0, uint symDbFlags = 0);
	void add(const DType* dtype);
	void add(DType&& dtype);
	void RemoveVaMacros();

	void ForEach(DTypeForEachCallback fn, bool& stop);
	DType* Find(FindData* fds);
	int FindExactList(LPCSTR sym, uint scopeID, DTypeList& cdlist);
	int FindExactList(uint symHash, uint scopeID, DTypeList& cdlist);
	DType* FindExact(std::string_view sym, uint scopeID, uint searchForType = 0, bool honorHideAttr = true);
	BOOL HasMultipleDefines(LPCSTR sym);
	DType* FindAnySym(const WTString& str);
	uint FindAny(const WTString& str, BOOL allowGoToMethod = FALSE);
	DType* FindMatch(DTypePtr symDat);
	// not guaranteed to return declaration - use ::FindDeclaration instead
	DType* FindDeclaration(DType& data);

	void GetMembersList(const WTString& sym, const WTString& scope, WTString& baseclasslist, ExpansionData* lb,
	                    bool exact);
	bool GetHint(const WTString& sym, const WTString& scope, WTString& baselist, WTString& hintstr);
	void MakeTemplate(const WTString& templateName, const WTString& instanceDeclaration,
	                  const WTString& templateFormalDeclarationArguments, const WTString& instanceArguments);
	VAHashTable* GetHashTable()
	{
		return mHashTable;
	}
	void FindDefList(WTString sym, bool matchScope, SymbolPosList& posList, bool matchBcl = false);
	void FindDefList(WTString sym, SymbolPosList& posList)
	{
		FindDefList(sym, true, posList);
	}
};

class FileDic
{
  public:
	FileDic(const CStringW& fname, BOOL isSysDic, uint rows);
	FileDic(const CStringW& fname, VAHashTable* pSharedHashTable);
	~FileDic();

	ULONG GetCount();
	void ForEach(DTypeForEachCallback fn, bool& stop);
	// return symbol type of sym found in any scope
	uint FindAny(const WTString& sym);
	// return DType from any scope whose sym matches str
	DType* FindAnySym(const WTString& str);
	int FindExactList(LPCSTR sym, uint scopeID, DTypeList& cdlist, int max_items = std::numeric_limits<int>::max());

  private:
	int FindExactList_fast(uint symHash, uint scopeID, DTypeList& cdlist, int max_items = std::numeric_limits<int>::max());
	int FindExactList_orig(uint symHash, uint scopeID, DTypeList& cdlist, int max_items = std::numeric_limits<int>::max());
  public:
	int FindExactList(uint symHash, uint scopeID, DTypeList& cdlist, int max_items = std::numeric_limits<int>::max());

  private:
	template<bool fast>
	DType* FindImpl(FindData* fds);
  public:
	DType* Find(const WTString& str, int fdFlags = FDF_NONE);
	DType* Find(FindData* fds);

  private:
	template <bool fast>
	DType* FindExactImpl(const WTString& sym, uint scopeID, BOOL concatDefs = TRUE, uint searchForType = 0,
	                 bool honorHideAttr = true);
  public:
	DType* FindExact(const WTString& sym, uint scopeID, BOOL concatDefs = TRUE, uint searchForType = 0,
	                 bool honorHideAttr = true);

  private:
	template <bool fast>
	DType* FindExactObjImpl(const WTString& sym, uint scopeID, bool honorHideAttr);
  public:
	DType* FindExactObj(const WTString& sym, uint scopeID, bool honorHideAttr = true);

	void FindDefList(WTString sym, bool matchSope, SymbolPosList& posList, bool matchBcl = false,
	                 bool scopeInParens = false);
	void FindDefList(WTString sym, SymbolPosList& posList)
	{
		FindDefList(sym, true, posList);
	}
	DType* FindMatch(DTypePtr symDat);
	void ClassBrowse(LPCSTR scope, HWND hTree, HTREEITEM hItem, BOOL refresh);
	DType* FindImplementation(DType& data);
	// not guaranteed to return declaration - use ::FindDeclaration instead
	DType* FindDeclaration(DType& data);
	bool GetHint(const WTString sym, const WTString scope, WTString& baselist, WTString& hintstr);
	void GetCompletionList(const WTString& sym, const WTString& scope, WTString& baselist, ExpansionData* lstbox,
	                       bool exact);
	void GetMembersList(const WTString& scope, WTString& baseclasslist, ExpansionData* lb);
	void GetMembersList(const WTString& scope, WTString& baseclasslist, DTypeList& dlist,
	                    bool filterBaseOverrides = false);
	void MakeTemplate(const WTString& templateName, const WTString& instanceDeclaration,
	                  const WTString& templateFormalDeclarationArguments, const WTString& instanceArguments,
	                  BOOL doMemberClasses = TRUE);
	WTString GetArgLists(LPCSTR symScope);
	WTString GetArgLists(const WTString& symScope)
	{
		return GetArgLists(symScope.c_str());
	}
	void SetDBDir(const CStringW& dir);
	CStringW GetDBDir() const
	{
		AutoLockCs l(mDbDirStringLock);
		return m_dbDir;
	}
	DType* GetFileData(const CStringW& fileAndPath) const;

	void SortIndexes();
	void add(const WTString& s, const WTString& d, uint type = 0, uint attrs = 0, uint dbFlags = 0, UINT fileID = 0,
	         int line = 0);
	void add(const DType* dtype);
	void add(DType&& dtype);

	// Dic compatibility
	void Flush();

	// Remove* should only be accessed from MultiParse
	void RemoveAllDefsFrom(const std::set<UINT>& fileIds);
	void RemoveAllDefsFrom_SinglePass(CStringW file);
	void RemoveAllDefsFrom_InitialPass(CStringW file);
	void RemoveMarkedDefs();

	void LoadComplete();
	void ReleaseTransientData(bool honorCountLimit = true);

	BOOL m_loaded;
	int m_modified;
	VAHashTable* GetHashTable()
	{
		return m_pHashTable;
	}

  private:
	CCriticalSection mHintStringsLock;
	mutable CSpinCriticalSection mDbDirStringLock;
	WTString m_lastuniqsymHint;
	WTString m_lastkeyHint;
	VAHashTable* m_pHashTable;
	BOOL m_isSysDic;
	CStringW m_dbDir;
	const uint m_rows;
	bool mHashTableOwner;
};

// see if list of \f separated items contains item
LPCSTR ContainsField(LPCSTR defs, LPCSTR def);
inline LPCSTR ContainsField(const WTString& defs, const WTString& def)
{
	return ContainsField(defs.c_str(), def.c_str());
}

void SubstituteTemplateInstanceDefText(const WTString& templateFormalDeclarationArguments,
                                       const WTString& constInstanceArguments, token2& def, bool& toTypeIsPtr,
                                       BOOL& didReplace);
DType* GetUnnamedParent(const WTString& childItemScope, const WTString& parentType);
WTString GetUnnamedParentScope(const WTString& childItemScope, const WTString& parentType);
void BrowseSymbol(LPCSTR symScope, HWND hTree, HTREEITEM hItem);
inline void BrowseSymbol(const WTString& symScope, HWND hTree, HTREEITEM hItem)
{
	return BrowseSymbol(symScope.c_str(), hTree, hItem);
}
void ClearFdicStatics();
bool IsFromInvalidSystemFile(const DType* dt);

////////////////////////////////////
// Idx Files:
// Now there are 27x2 data files(_ && a-z). Two for each letter,
// sorted and unsorted.  Search quickly populates using
// the sorted info and then adds  unsorted info from hopefully
// smaller file, so Sorting is no longer needed before expansion
// so we can sort on close.  Sort only sorts files that have been
// changes so it is much faster.
#define LETTER_FILE(theDbDir, c) (theDbDir + L"VA_" + c + L".idx")
#define LETTER_FILE_UNSORTED(theDbDir, c) (theDbDir + L"VA_" + c + L".tmp")
