#ifndef EDCNT
#define EDCNT

#include "ter_mfc.h"
#include "parse.h"
#include "mparse.h"
#include "bpoint.h"
#include "ScopeInfo.h"
#include "tree_state.h"
#include "EdCnt_fwd.h"
#include "..\VaMef\VaMef\MouseCmdEnums.cs"
#include "CWndDpiAware.h"

#define EditID 101
#define CM_DELETE 323
#define ID_EDIT_DRAGNDROP 0xeeee

class VADialog;
interface ITextDocument;

enum
{
	BEGWORD = -1,
	ENDWORD = 1,
	NEXTWORD = 2,
	PREVWORD = 3
};

class MPickList;
class BorderCWnd;

class EdCnt : public CTer, public CDpiAware<EdCnt>
{
	DECLARE_DYNCREATE(EdCnt)
	friend class ArgToolTip;
	friend class AttrLst;
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
	friend struct RunningDocumentData;
#endif
	bool undoLock;

  public:
	// private:
	ScopeInfoPtr ScopeInfoPtr()
	{
		AutoLockCs l(mDataLock);
		return m_pmparse;
	}
	int m_ScopeLangType;
	int m_ReparseScreen;                   // Screen needs reparsing
	bool m_FileHasTimerForFullReparse;     // Once all modifications are done, file will be queued
	bool m_FileHasTimerForFullReparseASAP; // Preserve ASAP status
	bool m_FileIsQuedForFullReparse;
	BOOL modified;
	bool modified_ever; // signals file has been changed since open
	uint m_lastScopePos;
	int LastLn;
	int m_hpos;    // used to test for ds window scrolls
	int m_vpos;    // used to test for ds window scrolls
	bool m_typing; // CurWord gets sym to left of caret while typing
	std::unique_ptr<AttrLst> m_LnAttr;

	WTString m_lastScope, mTagParserScopeType;
	int m_ftype;
	std::unique_ptr<BorderCWnd> m_lborder;

	// minimize WordPos Calls
	int m_tootipDef;
	ArgToolTip *m_ttParamInfo, *m_ttTypeInfo;
	BOOL m_doFixCase; // so we don't fix case of symbol after undo
	int m_minihelpHeight;
	bool m_ignoreScroll; // used to check for unpredicted scrolls

	ULONG m_lastEditPos;
	WTString m_lastEditSymScope;

  public:
	int mruFlag;
	int m_modCookie;
	char m_autoMatchChar;
	uint m_undoOnBkSpacePos;
	HWND m_hParentMDIWnd = nullptr;
	bool m_stop;
	FileDic* GetSysDicEd()
	{
		return GetSysDic(m_ftype);
	}
	MultiParsePtr GetParseDb()
	{
		AutoLockCs l(mDataLock);
		return m_pmparse;
	}
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
	MultiParsePtr GetNewestParseDb()
	{
		AutoLockCs l(mDataLock);
		if (mPendingMp && mPendingMp->GetFilename() == filename)
			return mPendingMp;
		return m_pmparse;
	}
#endif
	bool SetNewMparse(MultiParsePtr mp, BOOL setFlag);
	WTString GetSymScope() const
	{
		AutoLockCs l(mDataLock);
		return SymScope;
	}
	WTString GetSymDef() const
	{
		AutoLockCs l(mDataLock);
		return SymDef;
	}
	DType GetSymDtype() const
	{
		AutoLockCs l(mDataLock);
		return SymType;
	}
	uint GetSymDtypeType() const
	{
		AutoLockCs l(mDataLock);
		return SymType.type();
	}
	uint GetSymDtypeAttrs() const
	{
		AutoLockCs l(mDataLock);
		return SymType.Attributes();
	}
	void UpdateSym(DType* dt);

	WTString m_lastMinihelpDef;
	WTString m_curArgToolTipArg;
	ITextDocument* m_ITextDoc;
	BOOL m_txtFile; // unsupported language, do not parse.
	BOOL m_contextMenuShowing;
	BOOL m_skipUnderlining;
	CPoint m_reparseRange;
	WTString m_cwd;
	WTString m_cwdLeftOfCursor;
	int m_preventGetBuf; // Preserves "default" indentation in VC6
	BOOL m_isValidScope;
	BOOL m_hasRenameSug;
	tree_state::storage* m_vaOutlineTreeState;

	DicPtr LDictionary()
	{
		AutoLockCs l(mDataLock);
		return m_pmparse->LDictionary();
	}
	inline const CStringW FileName()
	{
		AutoLockCs l(mDataLock);
		return filename;
	}

	inline const CStringW ProjectName()
	{
#if defined(RAD_STUDIO)
		_ASSERTE(!"ProjectName is not implemented for RAD_STUDIO");
#endif
		return mProjectName;
	}

	inline int ProjectIconIdx()
	{
#if defined(RAD_STUDIO)
		_ASSERTE(!"ProjectName is not implemented for RAD_STUDIO");
#endif
		return mProjectIcon;
	}

	inline HWND GetDpiHWND() override
	{
		return m_hWnd;
	}

	EdCnt();
	virtual void Init(EdCntPtr pThis, HWND parent, void* pDoc, const CStringW& file);
	virtual void Reformat(int startIndex, int startLine, int endIndex, int endLine);
	~EdCnt();

	void SetStatusInfo();
	// when NULL is passed for proj, current project is used
	void UpdateMinihelp(LPCWSTR context, LPCWSTR def, LPCWSTR proj = nullptr, bool async = true);
	void ClearMinihelp(bool async = true);

	inline BOOL Modified()
	{
		return modified;
	}

	BOOL HasFocus()
	{
		return vGetFocus() == this;
	}

	// [case: 165029] this method detects focus properly in VS2010+
	// in WPF focus works differently -> there is focus shared between hosted elements
	// this method invokes method IsKeyboardFocusWithin of VisualElement of the view
	BOOL IsKeyboardFocusWithin();

	void SetFocusParentFrame();
	BOOL VAProcessKey(uint key, uint repeatCount, uint flags);
	afx_msg void OnChar(uint, uint, uint);
	afx_msg void OnClose()
	{
		CloseHandler();
	}
	afx_msg void OnKillFocus(CWnd*);
	afx_msg void OnSetFocus(CWnd*);
	afx_msg void OnMouseMove(uint keys, CPoint point);
	afx_msg void OnKeyDown(uint key, uint repeatCount, uint flags);
	afx_msg void OnSysKeyDown(uint key, uint repeatCount, uint flags);
	afx_msg void OnKeyUp(uint key, uint repeatCount, uint flags);
	afx_msg void OnSysKeyUp(uint key, uint repeatCount, uint flags);
	afx_msg void OnLButtonDown(uint modKeys, CPoint point);
	afx_msg void OnLButtonUp(uint modKeys, CPoint point);
	afx_msg void OnLButtonDblClk(uint modKeys, CPoint point);
	afx_msg void OnRButtonDown(uint modKeys, CPoint point);
	afx_msg void OnRButtonUp(uint modKeys, CPoint point);
	afx_msg void OnMButtonDown(uint modKeys, CPoint point);
	afx_msg void OnMButtonUp(uint modKeys, CPoint point);
	afx_msg void OnFileClose()
	{
		CloseHandler();
	}
	afx_msg BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg LRESULT OnPostponedSetFocus(WPARAM wParam, LPARAM lParam);
	void CloseHandler();
	int FixUpFnCall();

	// Mouse commands
	bool OnMouseCmd(VaMouseCmdBinding binding, CPoint pt = CPoint());
	bool OnMouseWheelCmd(VaMouseWheelCmdBinding binding, int wheel, CPoint pt = CPoint());

	// test methods,
	void CmUnMarkAll();
	void CmHelp();
	int Help();
	BOOL CheckForSaveAsOrRecycling();
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
	void RsEditorConnected(bool initialConnection);
	void RsEditorDisconnected();
	void RsParamCompletion();
#else
	void SetIvsTextView(IVsTextView* pView) override;
	void UpdateProjectInfo(IVsHierarchy* pHierarchy);
#endif
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnPaint();

	void CheckForDeletedFile(const CStringW& fpath);
	void CmEditCopy();
	void Copy();
	int CurLine(uint cp = -5)
	{ // Use NPOS for last line
		if (cp == -5)
			cp = CurPos();
		return LineFromChar((long)cp);
	}
	uint LinePos(int line = -5, int delta = 0)
	{ // Use (NPOS, 0) for last line
		if (line == -5)
			line = CurLine() + delta;
		return (uint)LineIndex(line);
	}
	void Reparse();
	bool SamePos(uint& p);
	// dont know how to test fore readonly
	void CmEditPasteMenu();
	void CodeTemplateMenu();
	void DisplayDefinitionList();
	void OpenOppositeFile();
	void OnRTFCopy(LPARAM wholeFile);
	void ContextMenu();
	void CmEditFindNext();
	void CmEditFindPrev();
	void CmEditExpand(int popType = 1);
	void CmEditExpandCmd()
	{
		CmEditExpand();
	}
	void SurroundWithBraces();
	// test stuff
	virtual LRESULT WindowProc(UINT, WPARAM, LPARAM);
	void SortSelectedLines();
	virtual LRESULT DefWindowProc(UINT, WPARAM, LPARAM);
	BOOL UpdateCompletionStatus(IVsCompletionSet* pCompSet, DWORD dwFlags);
	LRESULT FindNextReference(BOOL next /*= TRUE*/);
	char CharAt(uint pos)
	{
		if ((int)pos < 0)
		{
			ASSERT((int)pos >= -1);
			return '\0';
		}
		WTString buf = CTer::GetSubString(pos, pos + 1);
		return (buf.c_str()[0]);
	}
	int m_paintCommentLine;
	BOOL Insert(const char*, bool saveUndo = true, vsFormatOption format = noFormat,
	            bool handleLinebreaksAndIndentation = true);
	BOOL InsertW(CStringW txt, bool saveUndo = true, vsFormatOption format = noFormat,
	             bool handleLinebreaksAndIndentation = true);
	void InsertTabstr();

	WTString Scope(bool bParent = false, bool bSugestReaactoring = false);

	bool SuggestRefactoring();
	void ReparseScreen(BOOL underlineErrors, BOOL runAsync);

	uint WordPos(const WTString buf, int dir, uint offset = -1);
	uint WordPos(int dir = BEGWORD, uint offset = -1)
	{
		const WTString buf(GetBuf());
		return WordPos(buf, dir, offset);
	}

	uint WPos(int wc, uint cp = NPOS);
	WTString CurWord(int wc = 0, bool includeConnectedWords = false);
	WTString WordLeftOfCursor();
	WTString WordRightOfCursor();
	WTString WordRightOfCursorNext();
	WTString GetCurrentIndentation();
	WTString GetLine(int line);
	///////////////////////////////////////////////////////
	// Convert ulong to long for compatibility w/ CRichEdit
	void GetSel(ulong& up1, ulong& up2) const
	{
		long p1, p2;
		CTer::GetSel(p1, p2);
		up1 = (ulong)p1;
		up2 = (ulong)p2;
	}
	void GetSel(long& p1, long& p2) const
	{
		CTer::GetSel(p1, p2);
	}
	void SetSel(long p1, long p2);
	void SetSel(ulong p1, ulong p2)
	{
		SetSel((long)p1, (long)p2);
	}
	void SetSel(int p1, int p2)
	{
		SetSel((long)p1, (long)p2);
	}
	void SetSelection(long p1, long p2)
	{
		SetSel(p1, p2);
	}
	///////////////////////////////////////////////////////

	//	uint CurPosBegSel();
	uint CurPos(int end = false) const;
	uint SetPos(uint p)
	{
		SetSelection((long)p, (long)p);
		return p;
	}
	int m_LastPos1, m_LastPos2;
	void GetSel(long& StartPos, long& EndPos)
	{
		CTer::GetSel(StartPos, EndPos);
		m_LastPos1 = StartPos;
		m_LastPos2 = EndPos;
	}
	long GetBegWordIdxPos();
	WTString CurScopeWord();
	WTString GetSelString();
	CStringW GetSelStringW();
	WTString GetSubString(ulong p1, ulong p2);
	bool HasBookmarks();
	void CmToggleBookmark();
	void CmGotoNextBookmark();
	void CmGotoPrevBookmark();
	void CmClearAllBookmarks();
	void DisplayToolTipArgs(bool show, bool keepSel = false);
	void DisplayScope();
	bool DisplayTypeInfo(LPPOINT pPoint = NULL);
	bool GetTypeInfo(LPPOINT pPoint, WTString& typeInfo, CPoint* displayPt);
	WTString GetExtraDefInfo(DTypePtr data);
	WTString GetDefsFromMacro(const WTString& sScope, const WTString& sDef);
	void DisplayClassInfo(LPPOINT pPoint = NULL);
	void ClearAllPopups(bool noMouse = true);
	void Invalidate(BOOL bErase = TRUE); // T = redraw whole screen - lmargin, F = only one pixle so we do a bold brace
	WTString GetCommentForSym(const WTString& sym, int maxCommentLines);
	void GotoNextMethodInFile(BOOL next);
	BOOL ReplaceSel(const char* NewText, vsFormatOption vsFormat)
	{
		modified = TRUE;
		return CTer::ReplaceSel(NewText, vsFormat);
	}
	WTString GetBuf(BOOL force = FALSE);
	bool SpellCheckWord(CPoint point, bool interactive);
	void DisplaySpellSuggestions(CPoint point, const WTString& wd);
	DWORD QueryStatus(DWORD cmdId);
	HRESULT Exec(DWORD cmdId);
	void QueForReparse();
	void OnModified(BOOL startASAP = FALSE);

	BOOL SubclassWindow(HWND hWnd); // overridden so we can work with Resharper
	void SetReparseTimer();
	WTString GetSurroundText();
	void PositionDialogAtCaretWord(VADialog* dlg);
	CPoint GetCharPosThreadSafe(uint pos);
	LineMarkersPtr GetMethodsInFile();
	int ShowIntroduceVariablePopupMenuXP(int nrOfItems);
	bool CanSuperGoto();
	bool HasInitialParseCompleted() const
	{
		return mInitialParseCompleted;
	}
	double GetZoomFactor();

	bool IsIntelliCodeVisible();
	bool IsOpenParenAfterCaretWord();
	static void CreateDummyEditorControl(int ftype, MultiParsePtr& mp);

	DECLARE_MESSAGE_MAP()

  protected:
	LRESULT FlashMargin(WPARAM color, LPARAM duration);
	enum PosLocation
	{
		posAtCaret = -1,
		posBestGuess = 0,
		posAtCursor = 1
	};
	CPoint GetPoint(PosLocation posLocation) const;
	LRESULT GoToDef(PosLocation menuPos, WTString customScope = "");
	LRESULT SuperGoToDef(PosLocation posLocation, bool do_action = true);
	void ScrollTo(uint pos = NPOS, bool toMiddle = true);
	void GetCWHint(); // gets hint and suggest word for current word
	HDC GetDC()
	{
		if (m_hWnd)
			return ::GetDC(m_hWnd);
		return NULL;
	}
	void ReleaseDC(HDC dc)
	{
		::ReleaseDC(m_hWnd, dc);
	}
	long BufInsert(long pos, LPCSTR str);
	long BufInsert(long pos, const WTString& str)
	{
		return BufInsertW(pos, str.Wide());
	}
	long BufInsertW(long pos, const CStringW& str);

	afx_msg void OldLButtonDown(uint modKeys, CPoint point);
	afx_msg void OnContextMenu(CWnd* /*pWnd*/, CPoint point);
	BOOL ProcessReturn(bool doubleLineInsert = true);
	BOOL ProcessReturnComment();
	void ClearData();

  private:
	bool mInitialParseCompleted;
	CPoint mModifClickArmedPt;
	uint mModifClickKey;
	uint mLastDotToArrowPos;
	EdCntPtr mThis;
	LineMarkersPtr mMifMarkers;
	int mMifMarkersCookie;
	int mMifSettingsCookie;
	bool mMiddleClickDoCmd;
	bool mShiftRClickDoCmd;
	bool mUeRestoreSmartIndent; // [case: 109205]
	MultiParsePtr mPendingMp;   // protected by mDataLock
	MultiParsePtr m_pmparse;    // protected by mDataLock, access via GetParseDb()
	CStringW filename;          // protected by mDataLock, access via FileName()
	WTString SymScope;          // protected by mDataLock, access via GetSymScope()
	WTString SymDef;            // protected by mDataLock, access via GetSymDef()
	DType SymType;              // protected by mDataLock, access via GetSymDtype*()
	bool mUeRestoreAutoFormatOnPaste2;		// [case: 141666]
	CString mUeLastAutoFormatOnPaste2Value;	// [case: 141666]
	bool RestoreCaretPosition;
	long SavedCaretPosition;
	CStringW mProjectName;
	int mProjectIcon = 0;

	struct RecordedItem
	{
		RecordedItem(WTString* methodName, WTString searchSymScope, bool overloadResolution,
		             int gotoRelatedOverloadResolutionMode, DType ptr, std::vector<DType>& savedItems, CMenu* menu,
		             int depth)
		    : methodName(methodName), searchSymScope(searchSymScope), overloadResolution(overloadResolution),
		      gotoRelatedOverloadResolutionMode(gotoRelatedOverloadResolutionMode), ptr(ptr), cnt(cnt),
		      savedItems(savedItems), menu(menu), depth(depth)
		{
		}

		WTString* methodName;
		WTString searchSymScope;
		bool overloadResolution;
		int gotoRelatedOverloadResolutionMode;
		DType ptr;
		int cnt;
		std::vector<DType>& savedItems;
		CMenu* menu;
		int depth;
	};

	struct Separator
	{
		Separator(bool addSeparator)
		{
			AddSeparator = addSeparator;
		}

		bool AddSeparator;                             // have we added the separator yet
		int Resolved = 0;                              // nr. of items before the separator so far
		int Unresolved = 0;                            // nr. of items after the separator so far
		std::vector<RecordedItem> RecordedItemsBefore; // before the separator
		std::vector<RecordedItem> RecordedItemsAfter;  // after the separator
	};

	uint PopulateMenuFromRecordedEntries(Separator& separator, uint cnt, CMenu* menu);

	void PreInsert(long& begSelPos, long& p1, long& p2);
	void PostInsert(long begSelPos, long p1, long p2, vsFormatOption format, void* str, bool isWide);
	WTString IvsGetLineText(int line);
	void SaveBackup();
	void CheckRepeatedDotToArrow(uint pos);
	void ReleaseSelfReferences();
	bool GetSuperGotoSym(DType& outDtype, bool& isCursorInWs);
	void BuildInheritanceMenuItems(CMenu* menu, int symType, const WTString& scopeStr, std::vector<DType>& savedItems,
	                               int depth, uint& cnt, WTString* methodName, bool overloadResolution,
	                               Separator* separator = nullptr);
	uint AddInheritanceMenuItem(WTString* methodName, WTString searchSymScope, bool overloadResolution,
	                            int gotoRelatedOverloadResolutionMode, DType* ptr, uint cnt,
	                            std::vector<DType>& savedItems, CMenu* menu, int depth, int insertPos = -1);
	void BuildDeclsAndDefsMenuItems(DType* dtype, std::vector<DType>& items, class PopupMenuXP& xpmenu);
	void BuildHashtagUsageAndRefsMenuItems(DType* dtype, std::vector<DType>& items, class PopupMenuXP& xpmenu);
	void BuildCtorsMenuItem(DType* dtype, std::vector<DType>& items, class PopupMenuXP& xpmenu);
	uint BuildDefMenuItem(DType& dt, uint cnt, std::vector<DType>& items, CMenu* menu, bool allowNoPath = false);
	uint BuildHashtagMenuItem(DType& dt, uint cnt, std::vector<DType>& items, CMenu* menu, bool allowNoPath = false);
	uint BuildCtorMenuItem(DType& dt, uint cnt, std::vector<DType>& items, CMenu* menu, bool allowNoPath = false);
	void BuildIncludeMenuItems(CMenu* menu, CStringW file, bool includedBy, std::vector<DType>& savedItems, int depth,
	                           uint& cnt);
	void BuildTitleMenuItem(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu);
	void BuildTypeOfSymMenuItems(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu,
	                             DType* outType = nullptr);
	void DoBuildTypeOfSymMenuItems(DTypeList& dtypes, std::vector<DType>& items, CMenu* typesMenu, int depth, uint& cnt,
	                               DType* scopeType, DType* outVarType);
	void GetRelatedTypes(const DType* dtype, DTypeList& dtypes, DType* scopeType);
	void BuildMembersOfMenuItem(DType* dtype, std::vector<DType>& items, PopupMenuXP& xpmenu, bool displayName = false);
	bool InsertSnippetFromList(const std::list<int>& snippetIndexes);
	bool CmdEditGotoDefinition();
	// [case: 109205]
	void UeEnableUMacroIndentFixIfRelevant(bool checkPrevLine = false);
	void UeDisableUMacroIndentFixIfEnabled();
	void CheckForInitialGotoParse();
};

extern LRESULT TypeString(HWND, WTString&);
extern bool DidHelpFlag;

#define ID_HELPSEARCH_EDIT 0x7554 /* id of edit */
#define ID_RESULTSLIST_PARENT 0x7341

#define REPARSE_LINE -1
#define REPARSE_SCREEN -4
#define REPARSE_TO_CURSOR -5
#define REPARSE_PAINT_COMMENT_BLOCK -6

// global on/off settings
extern bool g_inMacro;

#define DISABLECHECK()                                                                                                 \
	{                                                                                                                  \
		if (g_inMacro || !Psettings->m_enableVA)                                                                       \
		{                                                                                                              \
			if (Psettings->m_catchAll)                                                                                 \
			{                                                                                                          \
				try                                                                                                    \
				{                                                                                                      \
					Default();                                                                                         \
					return;                                                                                            \
				}                                                                                                      \
				catch (...)                                                                                            \
				{                                                                                                      \
					VALOGEXCEPTION("EDH:");                                                                            \
					ASSERT(FALSE);                                                                                     \
				}                                                                                                      \
			}                                                                                                          \
			else                                                                                                       \
			{                                                                                                          \
				Default();                                                                                             \
				return;                                                                                                \
			}                                                                                                          \
		}                                                                                                              \
	}
#define DISABLECHECKCMD(cmd)                                                                                           \
	{                                                                                                                  \
		if (g_inMacro || !Psettings->m_enableVA)                                                                       \
		{                                                                                                              \
			if (Psettings->m_catchAll)                                                                                 \
			{                                                                                                          \
				try                                                                                                    \
				{                                                                                                      \
					return cmd;                                                                                        \
				}                                                                                                      \
				catch (...)                                                                                            \
				{                                                                                                      \
					VALOGEXCEPTION("EDH:");                                                                            \
					ASSERT(FALSE);                                                                                     \
				}                                                                                                      \
			}                                                                                                          \
			else                                                                                                       \
			{                                                                                                          \
				return cmd;                                                                                            \
			}                                                                                                          \
		}                                                                                                              \
	}

CStringW GetCommentForSym(LPCSTR sym, bool includeFileString = false, int maxCommentLines = 6,
                          int vsAlreadyHasComment = 0, bool includeBaseMethod = false);
inline CStringW GetCommentForSym(const WTString& sym, bool includeFileString = false, int maxCommentLines = 6,
                                 int vsAlreadyHasComment = 0, bool includeBaseMethod = false)
{
	return GetCommentForSym(sym.c_str(), includeFileString, maxCommentLines, vsAlreadyHasComment, includeBaseMethod);
}
CStringW StripCategoryAndMetaParameters(CStringW parameters);
CStringW IsolateCategories(CStringW parameters);
CStringW IsolateMetaParameters(CStringW parameters);
void GetUFunctionParametersForSym(LPCSTR sym, CStringW* outParams = nullptr, CStringW* outCategories = nullptr,
                                  CStringW* outMetaParams = nullptr);
BOOL EdPeekMessage(HWND h, BOOL forceCheck = FALSE);
BOOL HasVsNetPopup(BOOL);
bool IsUEMarkedType(LPCSTR sym);

extern EdCntPtr g_currentEdCnt;
extern CCriticalSection g_EdCntListLock;
extern std::vector<EdCntPtr> g_EdCntList;
EdCntPtr GetOpenEditWnd(HWND hWnd);
EdCntPtr GetOpenEditWnd(const CStringW& file);
EdCntPtr GetOpenEditWnd(LPCWSTR file);

#if !defined(RAD_STUDIO)
EdCntPtr GetOpenEditWnd(IVsTextBuffer* textBuffer);
#endif
EdCntPtr DelayFileOpen(const CStringW& file, int ln = 0, LPCSTR sym = NULL, BOOL preview = FALSE, BOOL func = FALSE);
inline EdCntPtr DelayFileOpen(const CStringW& file, int ln, const WTString& sym, BOOL preview = FALSE,
                              BOOL func = FALSE)
{
	return DelayFileOpen(file, ln, sym.c_str(), preview, func);
}
EdCntPtr DelayFileOpenLineAndChar(const CStringW& file, int ln = 0, int cl = 1, LPCSTR sym = NULL,
                                  BOOL preview = FALSE);
EdCntPtr DelayFileOpenPos(const CStringW& file, UINT pos, UINT endPos = -1, BOOL preview = FALSE);

extern uint wm_postponed_set_focus;

class VsSubWndVect
{
	std::vector<CWnd*> mVsWnds;

  public:
	void SubclassTooltip(HWND h, bool shouldColor);
	void SubclassBox(HWND h);
	void Delete(CWnd* item);
	void Shutdown();
};

extern VsSubWndVect gVsSubWndVect;


extern const WTString kExtraBlankLines;
inline bool RemovePadding_kExtraBlankLines(WTString& str)
{
	if (str.EndsWith(kExtraBlankLines))
	{
		str = str.Mid(0, str.GetLength() - kExtraBlankLines.GetLength());
		return true;
	}
	else
		return false;
}


#if defined(_DEBUGxx) && !defined(SEAN)
#define USE_NEW_MRU_TREE
#endif // _DEBUG && !SEAN

#endif
