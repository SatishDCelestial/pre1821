//////////////////////////////////////////////////////////////////////////
// Gets Base Class list for a symbol

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <vector>
#include "Mparse.h"

class DType;
typedef std::vector<WTString> WTStringV;

class BaseClassFinder
{
	WTStringV mSymScopeList;
	WTStringV mBaseClassList;
	WTString mInitialSymscope;
	MultiParsePtr m_mp;
	BOOL m_force;
	int m_deep;
	int mDevLang;
	volatile const INT* mQuit;
	DType* mInitialDat;
	bool mUsedDetachedData;
	bool mInitialSymIsTemplate;

  public:
	BaseClassFinder(int devLang)
	    : m_deep(0), mDevLang(devLang), mQuit(NULL), mInitialDat(nullptr), mUsedDetachedData(false),
	      mInitialSymIsTemplate(false)
	{
	}
	WTString GetBaseClassList(MultiParsePtr mp, DType* pDat, const WTString& symScope, BOOL force = FALSE,
	                          volatile const INT* bailMonitor = NULL);
	WTString GetBaseClassList(MultiParsePtr mp, const WTString& symScope, BOOL force = FALSE,
	                          volatile const INT* bailMonitor = NULL)
	{
		return GetBaseClassList(mp, nullptr, symScope, force, bailMonitor);
	}

  private:
	void Reset()
	{
		mSymScopeList.clear();
		mBaseClassList.clear();
	}
	DType* GetDataForSym(const WTString& symScope, bool exactOnly = false, bool* foundViaAlias = NULL);
	WTString GetBaseClassList(const WTString& symScope);
	DType* CreateOrUpdateCacheEntry(bool init, DType* previousCache, const WTString& cacheLookup, const WTString& bcl,
	                                DictionaryType dc, uint fileId = 0);
	bool IsRecursive(const WTString& bc) const;
	WTString GetTypesFromDef(DType* data);
	void QualifyTemplateArgs(const WTString& key, WTString& defaultTypes, WTString& theType);
	BOOL LoadFromCache(const WTString& cacheLookup, const WTString& symScope, DType*& data);
	DType* GetCachedBclDtype(const WTString& cacheLookup);
	BOOL MakeTemplateInstance(const WTString& symScope);
	bool InstantiateTemplateForSymbolScope(const WTString& symbolScope);
	void InvalidateCache(const WTString& cacheLookup);
	WTString GetCacheLookup(const WTString& symScope) const;
};

extern const WTString kInvalidCache;
extern const WTString kBclCachePrefix;
