#if !defined(AFX_VACOMPLETIONSET_H__8F76379A_A0C4_4D63_A6B2_30365CBC1F99__INCLUDED_)
#define AFX_VACOMPLETIONSET_H__8F76379A_A0C4_4D63_A6B2_30365CBC1F99__INCLUDED_
#include "expansion.h"
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <map>
#include "WTString.h"

#define MAXFILTERCOUNT 10000
#define ET_EXPAND_VSNET_SUGGEST_FLAG 3

class VACompletionBox;
class EdCnt;
class symbolInfo;
typedef symbolInfo* SymbolInfoPtr;

class VACompletionSet
{
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
	friend class VaRadStudioPlugin;
#endif
  protected:
	int count = 0;
	LONG m_xref[MAXFILTERCOUNT] = {0};
	int m_xrefCount = 0;
	long m_p1 = 0, m_p2 = 0;
	WTString m_startsWith, mPreviousStartsWith;
	LONG m_typeMask = 0;
	CPoint m_pt;
	bool m_AboveCaret = false;
	symbolInfo m_TmpVSNetSymInfo;
	IVsCompletionSet* m_pIVsCompletionSet = nullptr;
	WTString m_scope, m_bcl;
	ExpansionData m_ExpData;

	CImageList mVaDefaultImgList; // backing store
	int mVaDefaultImgListDPI = 0;

	CImageList mCombinedImgList; // backing store; is m_hVsCompletionSetImgList + va image lists
	int mCombinedImgListDPI = 0;
	GUID mCombinedImgListTheme = GUID_NULL;
	HANDLE m_hCurrentImgListInUse = nullptr;         // points to one of the backing stores
	HANDLE m_hVsCompletionSetImgList = nullptr;      // retrieved from current vs completion set
	HIMAGELIST m_hSourceOfCombinedImgList = nullptr; // used to invalidate mCombinedImgList
	int mVsListImgCount = 0; // number of images at start of mCombinedImgList that come from m_hVsCompletionSetImgList

	// With mixed content from their intellisense and our suggestion,
	// poptype is not really useful
	LONG m_expContainsFlags = 0; // can contain a mix of VSNET_TYPE_BIT_FLAG/SUGGEST_FILE_PATH/SUGGEST_MEMBERS/...
	DWORD mVsCompletionSetFlagsForExpandedItem = 0;
	bool m_displayUpToDate = false;
	bool mSpaceReserved = false;
	bool mDidFileCompletionRepos = false;

  public:
	LONG GetExpContainsFlags(int flag)
	{
		return m_expContainsFlags & flag;
	}
	LONG GetExpFlags()
	{
		return m_expContainsFlags;
	}
	void SetExpFlags(int flag)
	{
		m_expContainsFlags = flag;
	}
	EdCntPtr m_ed;
	ExpansionData* GetExpansionData()
	{
		return &m_ExpData;
	}
	VACompletionBox* m_expBox = nullptr;
	int m_expBoxDPI = 0;
	VACompletionBoxOwner* m_expBoxContainer = nullptr;
	CWnd* m_tBarContainer = nullptr;
	CToolBar* m_tBar = nullptr;
	CPoint m_caretPos;
	INT m_popType = 0;
	void SetPopType(INT popType);
	INT GetPopType() const
	{
		return m_popType;
	}
	bool m_isDef = false;
	bool m_isMembersList = false;
	bool IsMembersList() const
	{
		return m_isMembersList;
	}
	void IsMembersList(bool val)
	{
		m_isMembersList = val;
	}
	int GetBestMatch();
	WTString ToString(bool selectionState = false);
	VACompletionSet();
	virtual ~VACompletionSet();

	void TearDownExpBox();
	void FilterListType(LONG type);
	long GetCount(void);

	SymbolInfoPtr GetDisplayText(
	    /* [in] */ long iIndex,
	    /* [out] */ WTString& text,
	    /* [out] */ long* piGlyph);

	HANDLE GetImageList()
	{
		return m_hCurrentImgListInUse;
	}
	HANDLE GetDefaultImageList();
	virtual void SetImageList(HANDLE hImages);

	WTString GetDescriptionText(
	    /* [in] */ long iIndex, bool& shouldColorText);
	virtual WTString GetDescriptionTextOrg(
	    /* [in] */ long iIndex, bool& shouldColorText);

	void DisplayList(BOOL forceUpdate = FALSE);
	virtual void Dismiss();
	BOOL ProcessEvent(EdCntPtr ed, long key, LPARAM flag = WM_KEYDOWN, WPARAM wparam = 0,
	                  LPARAM lparam = 0); // wparam/lparam currently valid only for WM_MOUSEWHEEL
	void Reposition();

	SymbolInfoPtr GetSymbolInfo();
	BOOL IsExpUp(EdCntPtr ed);
	virtual BOOL HasSelection(); // Automation flag to see when it is OK to complete.

	//	HRESULT GetInitialExtent(
	//		/* [out] */ long *piLine,
	//		/* [out] */ long *piStartCol,
	//		/* [out] */ long *piEndCol);
	//
	//	HRESULT GetBestMatch(
	//		/* [in] */ const WCHAR *pszSoFar,
	//		/* [in] */ long iLength,
	//		/* [out] */ long *piIndex,
	//		/* [out] */ DWORD *pdwFlags);
	//
	//	HRESULT OnCommit(
	//		/* [in] */ const WCHAR *pszSoFar,
	//		/* [in] */ long iIndex,
	//		/* [in] */ BOOL fSelected,
	//		/* [in] */ WCHAR cCommit,
	//		/* [out] */ BSTR *pbstrCompleteWord);

	BOOL ShouldSuggest();
	void ShowCompletion(EdCntPtr ed);
	void ShowGueses(EdCntPtr ed, LPCSTR items, INT flag);
	void ShowGueses(EdCntPtr ed, const WTString& items, INT flag)
	{
		return ShowGueses(ed, items.c_str(), flag);
	}
	virtual void DoCompletion(EdCntPtr ed, int popType, bool fixCase);

	// 	void DoCompletion( int popType, bool fixCase );

	void CalculatePlacement();
	void PopulateAutoCompleteData(int popType, bool fixCase);
	void DisplayVSNetMembers(EdCntPtr ed, LPVOID data, LONG flags, UserExpandCommandState st, BOOL& expandDataRetval);
	virtual BOOL ExpandCurrentSel(char key = '\0', BOOL* didVSExpand = NULL);

	WTString GetCurrentSelString();
	bool IsFileCompletion() const;

	void ShowFilteringToolbar(bool show = true, bool allow_fadein = false);
	virtual BOOL IsVACompletionSetEx()
	{
		return FALSE;
	}
	virtual IVsCompletionSet* GetIVsCompletionSet()
	{
		return m_pIVsCompletionSet;
	}
	void RebuildExpansionBox();

  protected:
	BOOL ExpandInclude(EdCntPtr ed);
	BOOL FillFromText(EdCntPtr ed);
	virtual void ExpandIncludeDirectory(EdCntPtr ed, const WTString& selTxt, char key);
	bool PopulateWithFileList(bool isImport, EdCntPtr ed, int pos);
	void ClearContents();

	WTString StartsWith();
	void SelectStartsWith();
	void SetInitialExtent(long p1, long p2);
	void UpdateCurrentPos(EdCntPtr ed);
	virtual BOOL ShouldItemCompleteOn(symbolInfo* sinf, long c);
	WTString GetCompleteOnCharSet(symbolInfo* sinf, int lang);
	void DisplayFullSizeList();
	virtual bool AddString(BOOL sort, WTString str, UINT type, UINT attrs, DWORD symID, DWORD scopeHash,
	                       bool needDecode = true);
	virtual void OverrideSelectMode(symbolInfo* sinf, BOOL& unselect, BOOL& bFocusRectOnly)
	{
	}
	virtual void FixCase()
	{
	}
	void GetBaseExpScope();
	uint GetMemberScopeID(LPCSTR sym);
	bool UpdateImageList(HIMAGELIST copyThis);
	bool CheckIsMembersList(EdCntPtr ed);
	struct FileInfoItem
	{
		WTString mDisplayName;
		CStringW mFullpath;
		int mType;

		FileInfoItem() : mType(0)
		{
		}

		FileInfoItem(const WTString& name, const CStringW& path, int type)
		    : mDisplayName(name), mFullpath(path), mType(type)
		{
		}

		FileInfoItem(const WTString& name, LPCWSTR path, int type) : mDisplayName(name), mFullpath(path), mType(type)
		{
		}
	};

	typedef std::map<UINT, FileInfoItem> CompletionSetFileInfo;
	typedef CompletionSetFileInfo::const_iterator FileInfoIter;
	CompletionSetFileInfo mSetFileInfo;
	int m_IvsBestMatchCached = -1;
	DWORD m_IvsBestMatchFlagsCached = 0;
	bool mRebuildExpBox = true;
	bool m_ManuallySelected = false; // User pressed up/down to select this item from the list.
};

extern VACompletionSet* g_CompletionSet;
extern int g_ExImgCount;
WTString CompletionSetEntry(LPCSTR sym, UINT type = -1); // returns: str + "\001" + ITOS(type) + "\002"
inline WTString CompletionSetEntry(const WTString& sym, UINT type = -1)
{
	return CompletionSetEntry(sym.c_str(), type);
}
bool ShouldSuppressVaListbox(EdCntPtr ed);

enum
{
	CS_None = 0,
	CS_ExactMatch = 1,
	CS_MostLikely = 02,
	CS_ExactMatchNC = 3,
	CS_StartsWith = 5,
	CS_StartsWithNC = 7,
	CS_AcronymMatch = 8,
	CS_ContainsChars = 9
};
enum ContainsSubsetFlags
{
	MATCHCASE = 0x1,
	SUBSET = 0x2,
	ACRONYM = 0x4
};
int ContainsSubset(LPCSTR text, LPCSTR subText, UINT flag);
inline int ContainsSubset(const WTString& text, const WTString& subText, UINT flag)
{
	return ContainsSubset(text.c_str(), subText.c_str(), flag);
}
WTString TruncateString(const WTString& str);

#endif
