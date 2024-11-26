#pragma once

#include <vector>
#include "WTString.h"
#include "Lock.h"
#include "FOO.H"
#include <memory>
#include "EdCnt_fwd.h"
#include <set>

class IFindReferencesThreadObserver;
class FindReferences;
typedef std::shared_ptr<FindReferences> FindReferencesPtr;

enum FREF_TYPE
{
	FREF_None,
	FREF_Definition,       // int x;
	FREF_DefinitionAssign, // int x = 1;
	FREF_Reference,
	FREF_ReferenceAssign,
	FREF_Unknown,
	FREF_Comment,          // comments and strings
	FREF_JsSameName,       // Javascript only; any sym with same name as symbol being found
	FREF_ReferenceAutoVar, // auto x = ...;
	FREF_IncludeDirective, // hybrid behavior, displayed with comments but otherwise treated as unknown
	FREF_ScopeReference,   // foo in foo::Method
	FREF_Creation,         // new ClassName, ClassName obj(1), etc.
	FREF_Creation_Auto,
	FREF_Last // also used for refs filter mask of inherit/override attribute
};

enum FREF_FLAG
{
	FREF_Flg_Reference = 0x1,
	FREF_Flg_Reference_Include_Comments = 0x2,
	FREF_Flg_FindErrors = 0x4,
	FREF_Flg_InFileOnly = 0x8,
	FREF_FLG_FindAutoVars = 0x10,
	FREF_Flg_Convert_Enum = 0x20,  // [case: 93116]
	FREF_Flg_UeFindImplicit = 0x40, // [case: 141287] include hits for *_Implementation and *_Validate methods
	FREF_Flg_CorrespondingFile = 0x80 // valid only with FREF_Flg_InFileOnly
};

class FindReference
{
	bool mShouldDisplay;
	ULONG mInitialPos;

  public:
	CStringW mProject; // optional
	CStringW file;
	UINT fileId;
	ULONG lineNo;
	ULONG lineIdx;
	ULONG pos; // -1 == use lineNo/lineIdx
	FREF_TYPE type;
	WTString lnText;
	BOOL overridden;
	enum RefState
	{
		rsUnmodified,
		rsRefactored,
		rsModified
	};
	RefState dirty;
	DTypePtr mData;

#if defined(RAD_STUDIO)
	int RAD_pass = 0;
#endif

	FindReference(const CStringW& project, const CStringW& file, ULONG lineNo, ULONG lineIdx, ULONG pos, FREF_TYPE type,
	              LPCSTR lnText);

	FindReference(const FindReference& ref)
	    : mShouldDisplay(ref.mShouldDisplay), mInitialPos(ref.mInitialPos), mProject(ref.mProject), file(ref.file),
	      fileId(ref.fileId), lineNo(ref.lineNo), lineIdx(ref.lineIdx), pos(ref.pos), type(ref.type),
	      lnText(ref.lnText), overridden(ref.overridden), dirty(ref.dirty)
	{
	}

	bool ShouldDisplay() const
	{
		return mShouldDisplay;
	}
	void Hide()
	{
		mShouldDisplay = false;
	}
	ULONG GetPos() const;
	ULONG GetEdBufCharOffset(EdCntPtr ed, const WTString& symName, const WTString& buf = NULLSTR) const;
	void RevertChanges();
	void CommitChanges();
	bool IsEquivalent(const FindReference& rhs);
};

class FindReferences 
{
	friend class FindRefsScopeInfo;

  public:
	BOOL m_doHighlight;
	INT flags;
	std::set<WTString> elements; // used to store enum items
#if defined(RAD_STUDIO)
	int RAD_pass = 0;
#endif

	FindReferences(); // ctor used for threaded auto reference highlights
	FindReferences(int typeImgIdx, LPCSTR symscope = NULL, bool isRerun = false, bool findAutoVars = true);
	FindReferences(const FindReferences& refs, bool isClone = true);
	~FindReferences();

	void Init(LPCSTR symscope, int typeImgIdx, bool clear = true);
	void Init(const WTString& symscope, int typeImgIdx, bool clear = true)
	{
		return Init(symscope.c_str(), typeImgIdx, clear);
	}
	void SetSharedThis(FindReferencesPtr pThis)
	{
		_ASSERTE(pThis.get() == this);
		mThis = pThis;
	}
	BOOL ShouldFindAutoVars() const
	{
		if (!(flags & FREF_FLG_FindAutoVars))
			return FALSE;
		return mSymCanBeUsedAsAuto;
	}

	void ShouldFindAutoVars(bool findAutoVars)
	{
		if (findAutoVars)
			flags |= FREF_FLG_FindAutoVars;
		else
			flags &= ~FREF_FLG_FindAutoVars;
	}
	void UpdateSymForRename(const WTString& sym, const WTString& scope);
	WTString GetFindScope() const
	{
		return mSymScope;
	}
	WTString GetFindSym() const
	{
		return mSym;
	}
	WTString GetRenamedSym() const
	{
		return mOldSym;
	}
	uint GetSymFileId() const
	{
		return mSymFileId;
	}
	void SetObserver(IFindReferencesThreadObserver* obs)
	{
		_ASSERTE(!mObserver || obs == NULL);
		mObserver = obs;
	}

	bool IsBclInited() const
	{
		AutoLockCs l(mBclLock);
		return m_BCL_ary.IsInited();
	}
	void InitBcl(MultiParse* mp, int langType);
	int BclContains(uint hsh) const
	{
		AutoLockCs l(mBclLock);
		return m_BCL_ary.Contains(hsh);
	}

	FindReference* Add(const CStringW& project, const CStringW& file, ULONG lineNo, ULONG lineIdx, ULONG pos,
	                   FREF_TYPE type, LPCSTR lineText);
	BOOL GotoReference(int i, bool goto_first_line = false, int selLengthOverride = -1);
	// locks db dir; assumes single, non-concurrent find refs call to SearchFile
	int SearchFile(const CStringW& project, const CStringW& file, WTString* buf = (WTString*)NULL);
	// does not lock db dir; up to caller to ensure thread-safe; concurrent search
	void SearchFile(const CStringW& project, const CStringW& file, volatile const INT* monitorForQuit);
	FREF_TYPE IsReference(const CStringW& file, ULONG line, ULONG xOffset);
	FindReference* GetReference(size_t i);
	bool HideReference(int i);
	INT GetFileLine(int i, CStringW& file, long& pos, WTString& linetext, bool strip_markers = true);
	size_t Count() const
	{
		return refVect.size();
	}
	int GetReferenceImageIdx() const
	{
		return mTypeImageIdx;
	}
	void RemoveLastUnrelatedComments(const CStringW& fromFile);
	void AddHighlightReferenceMarkers();
	void StopReferenceHighlights();
	bool IsAutoHighlightRef() const
	{
		return mIsAutoRef;
	}
	// used during concurrent find refs
	void FlushFileRefs(const CStringW& file, IFindReferencesThreadObserver* mObserver);

	void Redraw();
	CStringW GetSummary(DWORD filter = FREF_None) const;
	void DocumentModified(const WCHAR* filenameIn, int startLineNo, int startLineIdx, int oldEndLineNo,
	                      int oldEndLineIdx, int newEndLineNo, int newEndLineIdx, int editNo);
	void DocumentSaved(const WCHAR* filename);
	void DocumentClosed(const WCHAR* filename);

	enum FindRefsSearchScope
	{
		searchFile,
		searchFilePair,
		searchProject,
		searchSolution,
		searchSharedProjects
	};
	FindRefsSearchScope GetScopeOfSearch() const
	{
		return mSearchScope;
	}
	void SetScopeOfSearch(FindRefsSearchScope scp)
	{
		mSearchScope = scp;
	}
	WTString GetScopeOfSearchStr() const;
	DType* GetOrigDtype()
	{
		return &mSymOrigData;
	}

  private:
	void RefineSearchSym();
	void ClearReferences();
	void RemoveHighlightReferenceMarkers();

	WTString mSym, mSymScope, mOldSym;
	DType mSymOrigData;
	uint mSymFileId;
	FindRefsSearchScope mSearchScope;

	// if acquiring both mTmpFileRefsLock and mVecLock, lock mTmpFileRefsLock first.
	mutable CSpinCriticalSection mVecLock;
	typedef std::vector<FindReference*> References;
	References refVect;

	mutable CSpinCriticalSection mTmpFileRefsLock;
	typedef std::map<CStringW, References> FileReferences;
	FileReferences mTmpFileRefs;

	const bool mIsClone;
	const bool mIsAutoRef;
	const bool mIsRerun;
	BOOL mSymCanBeUsedAsAuto;
	int mTypeImageIdx;

	mutable CSpinCriticalSection mBclLock;
	ScopeHashAry m_BCL_ary;                   // for derived and base classes
	IFindReferencesThreadObserver* mObserver; // only fires OnReference
	std::weak_ptr<FindReferences> mThis;
};

class FindRefsScopeInfo
{
	WTString mSym, mSymScope, mOldSym;
	uint mSymFileId;
	FindReferences::FindRefsSearchScope mSearchScope;

 public:
	FindRefsScopeInfo(const FindReferences& src)
	{
		GetRefsScopeInfo(src);
	}

	WTString GetFindScope() const
	{
		return mSymScope;
	}
	WTString GetFindSym() const
	{
		return mSym;
	}
	WTString GetRenamedSym() const
	{
		return mOldSym;
	}
	uint GetSymFileId() const
	{
		return mSymFileId;
	}

	void GetRefsScopeInfo(const FindReferences& src)
	{
		mSym = src.mSym;
		mSymScope = src.mSymScope;
		mOldSym = src.mOldSym;
		mSymFileId = src.mSymFileId;
		mSearchScope = src.mSearchScope;
	}

	void SetRefsScopeInfo(FindReferences& dst)
	{
		dst.mSym = mSym;
		dst.mSymScope = mSymScope;
		dst.mOldSym = mOldSym;
		dst.mSymFileId = mSymFileId;
		dst.mSearchScope = mSearchScope;
	}
};

extern FindReferencesPtr g_References;
