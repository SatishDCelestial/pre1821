#ifndef INC_EXPANSION_H
#define INC_EXPANSION_H

#include "OWLDefs.h"
#include "EdCnt.h"
#include "log.h"
#include "VaMessages.h"
#include "ArgToolTip.h"
#include "resource.h"
#include "StatusWnd.h"
#include "fdictionary.h" // WTHashKey
#include "VACompletionBox.h"

enum
{
	ICONIDX_NONE = -1,
	ICONIDX_TOMATO = 0,
	ICONIDX_CHECKALL,
	ICONIDX_UNCHECKALL,
	ICONIDX_SIW,
	ICONIDX_FIW,
	ICONIDX_UNUSED_3,
	ICONIDX_OPEN_OPPOSITE,
	ICONIDX_VATEMPLATE,
	ICONIDX_UNUSED_4,
	ICONIDX_UNUSED_5,
	ICONIDX_REPARSE,
	ICONIDX_SHOW_NONINHERITED_ITEMS_FIRST,
	ICONIDX_UNUSED_6,
	ICONIDX_UNUSED_7,
	ICONIDX_SCOPESYS,
	ICONIDX_SCOPEPROJECT,
	ICONIDX_SCOPELOCAL,
	ICONIDX_SCOPECUR,

	ICONIDX_REFERENCE_FIND_REF,
	ICONIDX_UNUSED_8,
	ICONIDX_UNUSED_9,
	ICONIDX_REFERENCE_GOTO_DECL,
	ICONIDX_REFERENCEASSIGN,
	ICONIDX_REFERENCE,
	ICONIDX_REFERENCE_GOTO_DEF,
	ICONIDX_REFACTOR_RENAME,
	ICONIDX_REFACTOR_EXTRACT_METHOD,
	ICONIDX_REFACTOR_ENCAPSULATE_FIELD,
	ICONIDX_REFACTOR_CHANGE_SIGNATURE,
	ICONIDX_REFACTOR_INSERT_USING_STATEMENT,
	ICONIDX_BLANK,
	ICONIDX_BULLET,
	ICONIDX_COMMENT_BULLET,

	ICONIDX_TOMATO_BACKGROUND,
	ICONIDX_FILE_FIRST,                       // used by #include completion lists
	ICONIDX_FILE_FOLDER = ICONIDX_FILE_FIRST, // #34
	ICONIDX_FILE_CPP,
	ICONIDX_FILE_H,
	ICONIDX_FILE_CS,
	ICONIDX_FILE_TXT,
	ICONIDX_FILE,
	ICONIDX_FILE_HTML,
	ICONIDX_FILE_VB,
	ICONIDX_FILE_LAST = ICONIDX_FILE_VB, // #41 used by #include completion lists
	ICONIDX_MODFILE,
	ICONIDX_MODMETHOD,

	ICONIDX_UNUSED_11,
	ICONIDX_CUT,
	ICONIDX_COPY,
	ICONIDX_PASTE,
	ICONIDX_DELETE,
	ICONIDX_UNUSED_12,
	ICONIDX_SCOPE_SUGGEST_V11, // scope suggestion but only in vs2012+
	ICONIDX_EXPANSION,
	ICONIDX_RESWORD, // scope suggestions pre-vs2012 and reserved words
	ICONIDX_SUGGESTION,

	ICONIDX_SNIPPET_COMMENTLINE,
	ICONIDX_SNIPPET_BRACKETS,
	ICONIDX_SNIPPET_COMMENTBLOCK,
	ICONIDX_SNIPPET_IFDEF,
	ICONIDX_SNIPPET_PARENS,

	ICONIDX_BREAKPOINT_ADD,
	ICONIDX_BREAKPOINT_REMOVE_ALL,
	ICONIDX_BREAKPOINT_DISABLE_ALL,
	ICONIDX_BREAKPOINT_DISABLE,
	ICONIDX_REFERENCE_CREATION, // #63
	ICONIDX_BOOKMARK_REMOVE,
	ICONIDX_REFERENCEASSIGN_OVERRIDDEN,
	ICONIDX_REFERENCE_OVERRIDDEN,
	ICONIDX_VS11_SNIPPET,
	// NOTE: use up the UNUSED slots before adding more images to the
	// imglist; adding images requires ~110 ast test result updates

	ICONIDX_COUNT	// keep as last entry
};

#define VA_TB_CMD_FLG 0xffff0000

class symbolInfo
{
	friend class ExpansionData;

  public:
	UINT m_hash;
	UINT m_scopeHash;
	int m_idx;
	WTString mSymStr;
	UINT m_type;
	UINT mAttrs;

  private:
	symbolInfo* pNext;

  public:
	symbolInfo(UINT idx = 0, UINT symId = 0, UINT scopeHash = 0)
	    : m_hash(symId), m_scopeHash(scopeHash), m_idx((int)idx), m_type(0), mAttrs(0), pNext(NULL)
	{
	}

	bool IsFileType() const
	{
		if (mAttrs & V_FILENAME && IS_IMG_TYPE(m_type))
		{
			uint type = m_type;
			if (type & VA_TB_CMD_FLG)
				type &= 0xffff;
			const bool retval = (UINT)ICONIDX_FILE_FIRST <= type && (UINT)ICONIDX_FILE_LAST >= type;
			_ASSERTE(retval);
			return retval;
		}

		_ASSERTE(!(mAttrs & V_FILENAME));
		return false;
	}
};

class ExpansionData
{
  public:
	ExpansionData() : m_count(0), m_pFirstItem(nullptr)
	{
	}
	~ExpansionData()
	{
		Clear();
	}

	int GetCount()
	{
		return (int)m_count;
	}
	symbolInfo* AllocUniqueItemByIDAtTail(DWORD symID, DWORD scopeHash, UINT type);
	symbolInfo* AllocUniqueItemByID(DWORD symID, DWORD scopeHash, UINT type);

	symbolInfo* AddStringNoSort(WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
	                            bool needDecode = true);
	symbolInfo* AddStringAndSort(WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
	                             bool needDecode = true);
	void SortByScopeID(DWORD scopeHash);
	symbolInfo* FindItemByIdx(int idx);

	int FindIdxFromSym(LPCSTR sym)
	{
		if (!GetCount())
			return NULL;

		if (GetCount() != (int)mSymInfoIdx.size())
			SetupDefaultSort();

		_ASSERTE(m_pFirstItem);
		const uint symHash = WTHashKey(sym);
		symbolInfo* ptr = m_pFirstItem;
		for (int idx = 0; ptr; idx++)
		{
			if (symHash == ptr->m_hash)
				return idx;
			ptr = ptr->pNext;
		}
		return 0;
	}

	void AddScopeSuggestions(EdCntPtr ed);
	void Clear();

  private:
	void RemoveSymbolInfoForReadd(symbolInfo* ptr);
	// sort and optionally build index
	void SetupDefaultSort(BOOL doIndex = TRUE);
	// build index after sorted
	void BuildIndex();

  private:
	template <class _Ty> struct InsensitiveLess
	/*: public std::binary_function<_Ty, _Ty, bool>*/
	{
		int CharRank(int ch) const
		{
			if (wt_isalnum(ch))
				return 0; // alphabetical group (digits are sorted to top of this group)
			if (strchr("_", ch))
				return 1; // after alpha group
			if (strchr("~!", ch))
				return 2; // after alpha group
			else
				return -1; // before alpha group
		}

		bool LessThan(const _Ty& _Left, const _Ty& _Right) const
		{
			int retval = _tcsicmp(_Left.c_str(), _Right.c_str());
			if (!retval)
				retval = _tcscmp(_Left.c_str(), _Right.c_str()); // now, case-sensitive
			return retval < 0;
		}

		// functor for operator<
		bool operator()(const _Ty& _Left, const _Ty& _Right) const
		{
			int leftRank, rightRank;
			if (gTypingDevLang == Src)
			{
				leftRank = CharRank(_Left[0]);
				rightRank = CharRank(_Right[0]);
			}
			else
				leftRank = rightRank = 0;

			// [case: 118434]
			// https://www.fileformat.info/info/unicode/char/2605/index.htm
			// utf16: 0x2605
			// utf8: 0xE2 0x98 0x85
			if (0xe2 == (unsigned char)_Left[0] && 0x98 == (unsigned char)_Left[1] && 0x85 == (unsigned char)_Left[2])
				leftRank = -2;
			if (0xe2 == (unsigned char)_Right[0] && 0x98 == (unsigned char)_Right[1] &&
			    0x85 == (unsigned char)_Right[2])
				rightRank = -2;

			if (gTypingDevLang == Src)
			{
				if (leftRank == rightRank)
				{
					return LessThan(_Left, _Right);
				}
				else
				{
					return (leftRank < rightRank);
				}
			}
			else
			{
				if (leftRank || rightRank)
				{
					if (leftRank && !rightRank)
						return true;
					if (rightRank && !leftRank)
						return false;
				}

				return LessThan(_Left, _Right);
			}
		}
	};

	UINT m_count;

	// used to prevent duplicate sym hash ids
	// (tried unordered_map but erase() was killer slow in debug builds)
	// this collection includes mSymInfoBySymStr, mSymInfoHeadEntries and mSymInfoTailEntries
	typedef std::map<UINT, symbolInfo*> SymInfoSymIdMap;
	SymInfoSymIdMap mSymInfoBySymId;

	typedef std::map<WTString, symbolInfo*, InsensitiveLess<WTString>> SymInfoStrMap;
	// used for string sorting of syms
	SymInfoStrMap mSymInfoBySymStr;
	// some syms go specifically at top of list, in order of add
	SymInfoSymIdMap mSymInfoHeadEntries;
	// some syms go specifically at end of list, in order of add
	SymInfoSymIdMap mSymInfoTailEntries;

	// used for fast access by final sorted index (same contents as mSymInfoBySymId, different sort)
	typedef std::vector<symbolInfo*> SymInfoVec;
	SymInfoVec mSymInfoIdx;

	// this is set after sorted
	symbolInfo* m_pFirstItem;
};

enum UserExpandCommandState
{
	uecsNone,
	uecsCompleteWordBefore,
	uecsCompleteWordAfter,
	uecsCompleteWordAfter2,
	uecsListMembersBefore,
	uecsListMembersAfter,
	uecsListMembersAfter2
};
extern UserExpandCommandState gUserExpandCommandState;
extern UserExpandCommandState gLastUserExpandCommandState;
extern DWORD gLastUserExpandCommandTick;

int GetFileImgIdx(const CStringW& file, int defaultIdx = ICONIDX_FILE);

#define CompletionStr_SEPLine '\002'
#define CompletionStr_SEPType '\001'
#define CompletionStr_SEPLineStr "\002"
#define CompletionStr_SEPTypeStr "\001"
#define AUTOTEXT_SHORTCUT_SEPARATOR '\003'

#endif
