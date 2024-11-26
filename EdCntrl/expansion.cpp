#include "stdafxed.h"
#include <atlbase.h>
#include "expansion.h"
#include "resource.h"
#include "FontSettings.h"
#include "wtcsym.h"
#include "AutotextManager.h"
#include "FileTypes.h"
#include "VACompletionSet.h"
#include "VACompletionBox.h"
#include "..\Addin\DSCmds.h"
#include "VaTimers.h"
#include "..\common\ThreadName.h"
#include "DevShellAttributes.h"
#include "Settings.h"
#include "DBLock.h"
#include "project.h"
#include "file.h"
#include "WTString.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

UINT g_baseExpScope = 0;
UserExpandCommandState gUserExpandCommandState = uecsNone;
UserExpandCommandState gLastUserExpandCommandState = uecsNone;
DWORD gLastUserExpandCommandTick = 0;

void ExpansionData::SortByScopeID(DWORD scopeHash)
{
	SetupDefaultSort(FALSE);

	if (Psettings->m_bListNonInheritedMembersFirst)
	{
		// depends on default sort
		_ASSERTE(m_pFirstItem);
		// Sorting of a linked list is not really as easy as it sounds.
		symbolInfo* ptr = m_pFirstItem;
		symbolInfo* last = NULL;
		symbolInfo* top = NULL;

		while (ptr)
		{
			symbolInfo* nxt = ptr->pNext;
			if (ptr->m_scopeHash == scopeHash)
			{
				// remove from list
				if (last)
					last->pNext = nxt;
				// add to top
				if (!top)
				{
					if (ptr != m_pFirstItem)
						ptr->pNext = m_pFirstItem;
					top = m_pFirstItem = ptr;
				}
				else if (last)
				{
					ptr->pNext = top->pNext;
					top->pNext = ptr;
				}
				top = ptr;
			}
			else
				last = ptr;
			ptr = nxt;
		}
	}

	BuildIndex();

#ifdef _DEBUG
	// sanity check
	int n = 0;
	for (symbolInfo* ptr = m_pFirstItem; ptr; ptr = ptr->pNext, n++)
		;
	_ASSERTE(n == GetCount() && n == (int)mSymInfoIdx.size());
#endif
}

// caller must add return to mSymInfoBySymStr
symbolInfo* ExpansionData::AllocUniqueItemByID(DWORD symID, DWORD scopeHash, UINT type)
{
	symbolInfo* ptr;
	SymInfoSymIdMap::iterator it = mSymInfoBySymId.find(symID);
	if (it != mSymInfoBySymId.end())
	{
		ptr = (*it).second;
		_ASSERTE(ptr && ptr->mSymStr.GetLength());

		// give our smart suggestions priority over reserved words, etc
		// (even though same text is inserted)
		if (type & VSNET_TYPE_BIT_FLAG && !(ptr->m_type & VSNET_TYPE_BIT_FLAG))
			return nullptr;

		if (!(type & ET_MASK_RESULT) && (ptr->m_type & ET_MASK_RESULT))
			return nullptr;

		if (ptr->m_type == ET_AUTOTEXT_TYPE_SUGGESTION || ptr->m_type == ET_SCOPE_SUGGESTION)
			return nullptr;

		// reuse this ptr
		// do not change ptr->m_scopeHash - breaks Form1.h_ListNonInheritedMembersFirstCLI test
		RemoveSymbolInfoForReadd(ptr);
	}
	else
	{
		ptr = new symbolInfo(m_count++, symID, scopeHash);
		mSymInfoBySymId[symID] = ptr;
	}

	_ASSERTE((int)mSymInfoBySymId.size() == GetCount());
	return ptr;
}

// caller must add return to mSymInfoHeadEntries or mSymInfoTailEntries
symbolInfo* ExpansionData::AllocUniqueItemByIDAtTail(DWORD symID, DWORD scopeHash, UINT type)
{
	symbolInfo* ptr;
	SymInfoSymIdMap::iterator it = mSymInfoBySymId.find(symID);
	if (it != mSymInfoBySymId.end())
	{
		ptr = (*it).second;
		_ASSERTE(ptr && ptr->mSymStr.GetLength());
		if ((ptr->m_scopeHash && g_baseExpScope) || ptr->m_scopeHash == scopeHash)
			return nullptr;

		// give our smart suggestions priority over reserved words, etc
		// (even though same text is inserted)
		if (type & VSNET_TYPE_BIT_FLAG && !(ptr->m_type & VSNET_TYPE_BIT_FLAG))
			return nullptr;

		if (!(type & ET_MASK_RESULT) && (ptr->m_type & ET_MASK_RESULT))
			return nullptr;

		if (ptr->m_type == ET_AUTOTEXT_TYPE_SUGGESTION || ptr->m_type == ET_SCOPE_SUGGESTION)
			return nullptr;

		// reuse this ptr
		ptr->m_scopeHash = scopeHash;
		RemoveSymbolInfoForReadd(ptr);
	}
	else
	{
		ptr = new symbolInfo(m_count++, symID, scopeHash);
		mSymInfoBySymId[symID] = ptr;
	}

	_ASSERTE((int)mSymInfoBySymId.size() == GetCount());
	return ptr;
}

int GetFileImgIdx(const CStringW& file, int defaultIdx /*= ICONIDX_FILE*/)
{
	int ftype = GetFileType(file);
	switch (ftype)
	{
	case Src:
		return ICONIDX_FILE_CPP;
	case Header:
		return ICONIDX_FILE_H;
	case CS:
		return ICONIDX_FILE_CS;
	case Idl:
		return ICONIDX_FILE;
	}
	if (Is_Tag_Based(ftype))
		return ICONIDX_FILE_HTML;
	if (Is_VB_VBS_File(ftype))
		return ICONIDX_FILE_VB;

	return defaultIdx;
}

inline bool IsStr(LPCTSTR str)
{
	if (!str || !*str)
		return false;
	while (*str)
	{
		char ch = *str++;
		if (!ISCSYM(ch))
			return false;
	}
	return true;
}

bool ExptypeHasPriorityOverVsCompletion(INT expType)
{
	if (ET_SCOPE_SUGGESTION == expType || ET_SUGGEST == expType || ET_SUGGEST_BITS == expType ||
	    ET_AUTOTEXT == expType || ET_AUTOTEXT_TYPE_SUGGESTION == expType || ET_VS_SNIPPET == expType ||
	    ET_EXPAND_TEXT == expType || (ET_EXPAND_INCLUDE == expType && Psettings->mIncludeDirectiveCompletionLists))
	{
		return true;
	}

	return false;
}

symbolInfo* ExpansionData::AddStringNoSort(WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
                                           bool needDecode /*= true*/)
{
	// like a sorted listbox InsertString
	WTString tstr = needDecode ? DecodeScope(str) : str;
	if (!IS_IMG_TYPE(type) && COMMENT == type)
		return NULL; // filter out gotodefs

	if (tstr.Find("unnamed_") != -1 &&
	    (tstr.Find("unnamed_struct_") != -1 || tstr.Find("unnamed_union_") != -1 || tstr.Find("unnamed_enum_") != -1))
		return NULL;

	if (g_CompletionSet->m_popType != ET_SUGGEST_BITS && g_CompletionSet->m_popType != ET_SUGGEST && g_currentEdCnt &&
	    IsCFile(g_currentEdCnt->m_ScopeLangType) // Only insert "operator" in c/c++. case=40335
	    && type != ET_AUTOTEXT && type != ET_VS_SNIPPET && !(attrs & V_FILENAME) && strchr("+-[]<>=&", str[0]))
		tstr.prepend("operator");

	_ASSERTE(::GetCurrentThreadId() == g_mainThread);
	symbolInfo* ptr = AllocUniqueItemByIDAtTail(symID, scopeHash, type);
	if (!ptr)
		return NULL; // already have this string in the list box

	ptr->m_type = type;
	ptr->mAttrs = attrs;
	if (!ptr->mSymStr.IsEmpty())
	{
		// reusing a syminfo
		if (g_loggingEnabled && ptr->mSymStr != tstr)
		{
			vLog("MPL::non-unique %s %s %lx", ptr->mSymStr.c_str(), tstr.c_str(), symID);
		}
	}
	else
		ptr->mSymStr = tstr;

	if (mSymInfoBySymStr.size())
		mSymInfoTailEntries[(uint)mSymInfoTailEntries.size()] = ptr;
	else
		mSymInfoHeadEntries[(uint)mSymInfoHeadEntries.size()] = ptr;
	_ASSERTE((mSymInfoBySymStr.size() + mSymInfoTailEntries.size() + mSymInfoHeadEntries.size()) ==
	         mSymInfoBySymId.size());
	return ptr;
}

symbolInfo* ExpansionData::FindItemByIdx(int idx)
{
	if (!GetCount())
		return NULL;

	if (GetCount() != (int)mSymInfoIdx.size())
		SetupDefaultSort();

	if (idx < (int)mSymInfoIdx.size())
		return mSymInfoIdx[(size_t)idx];
	return NULL;
}

symbolInfo* ExpansionData::AddStringAndSort(WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
                                            bool needDecode /*= true*/)
{
	// like a sorted listbox AddString
	_ASSERTE((type & TYPEMASK) == type || (type & ET_MASK) == ET_MASK_RESULT || IS_IMG_TYPE(type));
	WTString tstr = needDecode ? DecodeScope(str) : str;
	if (g_CompletionSet->m_popType != ET_EXPAND_VSNET && g_CompletionSet->m_popType < 10 && type == COMMENT ||
	    tstr == "< >")
		return NULL; // filter out gotodefs

	if (g_CompletionSet->m_popType != ET_EXPAND_VSNET && g_CompletionSet->m_popType != ET_EXPAND_TEXT &&
	    type != ET_SCOPE_SUGGESTION && type != ET_AUTOTEXT_TYPE_SUGGESTION && type != ET_AUTOTEXT &&
	    type != ET_VS_SNIPPET && !IS_IMG_TYPE(type) && type != ET_SUGGEST_BITS &&
	    strncmp(str.c_str(), "operator ", 8) != 0 && !strchr("+-[]<>=&~!", str[0]) && !IsStr(tstr.c_str()) && type != 1)
	{
		if (!tstr.IsEmpty())
			vLog("MPL::PreFilter: %s", tstr.c_str());
		return NULL; // filter out operators
	}

	if (tstr.Find("unnamed_") != -1 &&
	    (tstr.Find("unnamed_struct_") != -1 || tstr.Find("unnamed_union_") != -1 || tstr.Find("unnamed_enum_") != -1))
		return NULL;

	if (g_CompletionSet->m_popType != ET_SUGGEST_BITS &&
	    !(type & VSNET_TYPE_BIT_FLAG) // Do not modify vsnet completions. XAML "<New Event Handler>"
	    && g_CompletionSet->m_popType != ET_SUGGEST && type != ET_AUTOTEXT && type != ET_VS_SNIPPET &&
	    !(attrs & V_FILENAME) && strchr("+-[]<>=&", str[0]))
		tstr.prepend("operator");

	//if (type == CLASS && tstr.EndsWith('>'))
	//{
	//	auto templStart = tstr.Find("<");
	//	if (templStart > 0)
	//	{
	//		WTString rootName = tstr.Left(templStart);
	//		AddStringAndSort(rootName, CLASS, 0, WTHashKey(rootName), scopeHash, false);
	//	}
	//}


	_ASSERTE(::GetCurrentThreadId() == g_mainThread);
	symbolInfo* ptr = AllocUniqueItemByID(symID, scopeHash, type);
	if (!ptr)
		return NULL; // already have this string in the list box

	if (!ptr->mSymStr.IsEmpty())
	{
		// reused ptr
		if (type == ET_AUTOTEXT || type == ET_VS_SNIPPET)
		{
			ptr->mSymStr = str;
			ptr->m_type = type;
			ptr->mAttrs = 0;
		}
		else if (g_loggingEnabled && ptr->mSymStr != tstr)
		{
			vLog("MPL::non-unique %s %s %lx", ptr->mSymStr.c_str(), tstr.c_str(), symID);
		}
	}
	else
	{
		ptr->m_type = type;
		ptr->mAttrs = attrs;
		ptr->mSymStr = tstr;
	}

	mSymInfoBySymStr[ptr->mSymStr] = ptr;
	_ASSERTE((mSymInfoBySymStr.size() + mSymInfoTailEntries.size() + mSymInfoHeadEntries.size()) ==
	         mSymInfoBySymId.size());
	return ptr;
}

void ExpansionData::AddScopeSuggestions(EdCntPtr ed)
{
	if (!ed)
		return;

	ScopeInfoPtr si(ed->ScopeInfoPtr());
	if (!si->HasScopeSuggestions())
		return;

	token2 t = si->m_ScopeSuggestions;
	WTString s;
	while (t.more() > 1)
	{
		token2 ln = t.read(CompletionStr_SEPLine);
		ln.read(CompletionStr_SEPType, s);
		static const WTString sep(AUTOTEXT_SHORTCUT_SEPARATOR);
		WTString title = TokenGetField(s, sep);
		uint type = (uint)atoi(ln.read().c_str());
		uint attrs = 0;
		if (UINT_MAX != type && (type & ET_MASK) != ET_MASK_RESULT && (type & VA_TB_CMD_FLG) != VA_TB_CMD_FLG &&
		    !IS_IMG_TYPE(type))
		{
			_ASSERTE((type & TYPEMASK) == type);
		}
		AddStringAndSort(s, type, attrs, WTHashKey(title), 1);
	}
}

void ExpansionData::Clear()
{
	_ASSERTE(::GetCurrentThreadId() == g_mainThread);
	if (!GetCount())
	{
		_ASSERTE(!m_pFirstItem && 0 == mSymInfoIdx.size() && 0 == mSymInfoBySymStr.size() &&
		         0 == mSymInfoHeadEntries.size() && 0 == mSymInfoTailEntries.size() && 0 == mSymInfoBySymId.size());
		return;
	}

	std::for_each(mSymInfoBySymId.begin(), mSymInfoBySymId.end(),
	              [](std::pair<UINT, symbolInfo*> pr) { delete pr.second; });

	m_pFirstItem = NULL;
	m_count = 0;
	mSymInfoBySymStr.clear();
	mSymInfoHeadEntries.clear();
	mSymInfoTailEntries.clear();
	mSymInfoIdx.clear();
	mSymInfoBySymId.clear();
}

void ExpansionData::SetupDefaultSort(BOOL doIndex /*= TRUE*/)
{
	mSymInfoIdx.clear();
	if (!GetCount())
	{
		_ASSERTE(m_pFirstItem == nullptr);
		return;
	}

	m_pFirstItem = nullptr;

	symbolInfo* last = nullptr;
	for (SymInfoSymIdMap::iterator it = mSymInfoHeadEntries.begin(); it != mSymInfoHeadEntries.end(); ++it)
	{
		symbolInfo* cur = (*it).second;
		if (m_pFirstItem == nullptr)
			m_pFirstItem = cur;
		else
			last->pNext = cur;

		cur->pNext = nullptr;
		last = cur;
	}

	for (SymInfoStrMap::iterator it = mSymInfoBySymStr.begin(); it != mSymInfoBySymStr.end(); ++it)
	{
		symbolInfo* cur = (*it).second;
		if (m_pFirstItem == nullptr)
			m_pFirstItem = cur;
		else
			last->pNext = cur;

		cur->pNext = nullptr;
		last = cur;
	}

	for (SymInfoSymIdMap::iterator it = mSymInfoTailEntries.begin(); it != mSymInfoTailEntries.end(); ++it)
	{
		symbolInfo* cur = (*it).second;
		if (m_pFirstItem == nullptr)
			m_pFirstItem = cur;
		else
			last->pNext = cur;

		cur->pNext = nullptr;
		last = cur;
	}

#ifdef _DEBUG
	// sanity check
	int n = 0;
	for (symbolInfo* ptr = m_pFirstItem; ptr; ptr = ptr->pNext, n++)
		;
	_ASSERTE(n == GetCount());
#endif

	if (doIndex)
		BuildIndex();
}

void ExpansionData::BuildIndex()
{
	mSymInfoIdx.clear();
	if (!GetCount())
	{
		_ASSERTE(m_pFirstItem == nullptr);
		return;
	}

	_ASSERTE(m_pFirstItem);

	mSymInfoIdx.reserve((uint)GetCount());
	for (symbolInfo* ptr = m_pFirstItem; ptr; ptr = ptr->pNext)
		mSymInfoIdx.push_back(ptr);

#ifdef _DEBUG
	// sanity check
	int n = 0;
	for (symbolInfo* ptr = m_pFirstItem; ptr; ptr = ptr->pNext, n++)
		;
	_ASSERTE(n == GetCount());
	_ASSERTE(GetCount() == (int)mSymInfoIdx.size());
#endif
}

void ExpansionData::RemoveSymbolInfoForReadd(symbolInfo* ptr)
{
	if (SymInfoStrMap::iterator it = mSymInfoBySymStr.find(ptr->mSymStr); it != mSymInfoBySymStr.end())
	{
		// ptr was in the sorted collection
		mSymInfoBySymStr.erase(it);
		return;
	}

	// since mSymInfoHeadEntries and mSymInfoTailEntries are sorted on position, need to iterate
	// over them to find out where ptr was stored

	for (SymInfoSymIdMap::iterator it = mSymInfoHeadEntries.begin(); it != mSymInfoHeadEntries.end(); ++it)
	{
		symbolInfo* cur = (*it).second;
		if (cur == ptr)
		{
			mSymInfoHeadEntries.erase(it);
			return;
		}
	}

	for (SymInfoSymIdMap::iterator it = mSymInfoTailEntries.begin(); it != mSymInfoTailEntries.end(); ++it)
	{
		symbolInfo* cur = (*it).second;
		if (cur == ptr)
		{
			mSymInfoTailEntries.erase(it);
			return;
		}
	}
}

int FileLineMarker::GetIconIdx() const
{
	if (COMMENT == mType)
		return ICONIDX_COMMENT_BULLET;
	if (RESWORD == mType)
		return ICONIDX_BULLET;
	return GetTypeImgIdx(mType, mAttrs);
}
