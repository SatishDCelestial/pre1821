#pragma once

#include "VARefactor.h"
#include <memory>

enum ScreenAttributeEnum
{
	SA_NONE = 0,
	SA_UNDERLINE,
	SA_UNDERLINE_SPELLING,
	SA_BRACEMATCH,
	SA_MISBRACEMATCH,
	SA_REFERENCE,
	SA_REFERENCE_ASSIGN,
	SA_REFERENCE_AUTO,
	SA_REFERENCE_ASSIGN_AUTO,
	SA_FIND_RESULT,
	SA_SIMPLE_WORD_HIGHLIGHT,
	SA_HASHTAG
};

class AttrClass;
typedef std::shared_ptr<AttrClass> AttrClassPtr;

class AttrClass
{
  public:
	enum BlockPositions
	{
		kUtf16Positions = 0xFFFFFFFE,
		kPotentiallyMbcsPositions = 0xFFFFFFFF,

		kBPMin = kUtf16Positions,
		kBPMax = kPotentiallyMbcsPositions,
	};

  private:
	friend class ScreenAttributes;
	AttrClass() : mFlag(SA_NONE), mAdornment(0), mEd(NULL)
	{
	}
	AttrClass(EdCntPtr pEd, const WTString& sym, ULONG line, ULONG xOffset, ScreenAttributeEnum flag, ULONG pos = 0);
	AttrClass(EdCntPtr pEd, ScreenAttributeEnum flag, ULONG startPos, ULONG endPos,
	          BlockPositions bp = kUtf16Positions);
	AttrClass(const AttrClassPtr& rhs);

  public:
	~AttrClass();

	void Init(EdCntPtr pEd, const WTString& sym, ULONG line, ULONG xOffset, ScreenAttributeEnum flag, ULONG pos);
	void Init(EdCntPtr pEd, ScreenAttributeEnum flag, ULONG startPos, ULONG endPos,
	          BlockPositions bp = kUtf16Positions);

	void SetThis(AttrClassPtr attr)
	{
		mThis = attr;
	}
	void Display();
	void Invalidate();

	BOOL IsMatch(EdCntPtr ed, ULONG line, ULONG xOffset, LPCSTR sym);

	CStringW mSym;
	WTString mSymUtf8;
	ULONG mXOffset;
	ULONG mLine;
	ScreenAttributeEnum mFlag = SA_NONE;
	ULONG mPos;
	ULONG mEndPos; // valid if mSym is empty

  private:
	void Redraw();

	ULONG mXOffsetEnd;
	CComPtr<IVsTextLineMarker> mMarker;
	int mAdornment = 0;
	EdCntPtr mEd;

	// mThis is needed so that AttrClass can pass shared_ptr of itself to AttrMarkerClient
	std::weak_ptr<AttrClass> mThis;
};

class VATooltipBlocker
{
	static unsigned int blocks;

  public:
	VATooltipBlocker();
	~VATooltipBlocker();

	static bool IsBlock()
	{
		return blocks > 0;
	}
};

class VATomatoTip : public CBitmapButton
{
	CToolTipCtrl mToolTipCtrl;
	CPoint m_dispPt;
	long m_edPos;
	ULONG dx;
	BOOL m_hasTip;
	BOOL m_hasFocus;
	CStatic m_layeredParentWnd;
	BOOL m_IsSuggestionTip;

	enum timerId
	{
		TID_MovePopups = 'MPFR'
	};

  public:
	long m_lastEditPos;
	BOOL m_bContextMenuUp;

	VATomatoTip() : m_hasTip(FALSE), m_hasFocus(FALSE), m_IsSuggestionTip(FALSE), m_bContextMenuUp(FALSE)
	{
	}
	~VATomatoTip();

	static bool CanDisplay();
	static bool EnumThreadPopups(std::function<bool(HWND)> func, DWORD threadId = 0);
	static bool MovePopupsFromRect(const CRect& screenRect, const std::initializer_list<HWND>& ignoreWnd,
	                               DWORD threadId = 0);

	bool ShouldDisplayAtCursor(EdCntPtr ed);
	void DisplaySuggestionTip(EdCntPtr ed, int index);
	void Display(EdCntPtr ed, int index);
	void Display(EdCntPtr parent, CPoint pt);
	void Dismiss();
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnHover(EdCntPtr ed, CPoint pt);
	void DisplayTipContextMenu(bool atMousePos);
	void UpdateQuickInfoState(BOOL hasQuickInfo);
	WTString m_orgSymScope;
	WTString m_newSymName;
	static const WTString sButtonText;

	enum PositionPlacement
	{
		atCaret,
		atCursor
	};
	void DoMenu(PositionPlacement p);

  protected:
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

  private:
	bool ShouldDisplay(EdCntPtr ed, const WTString& edBuf);
	void DoMenu(LPPOINT pt, bool notSymbol = false);
	bool GetDisplayPoint(EdCntPtr ed, CPoint& pt, PositionPlacement at, bool* notSymbol = nullptr);
};

using VATomatoTipPtr = std::shared_ptr<VATomatoTip>;

class ScreenAttributes
{
  public:
	ScreenAttributes()
	{
		m_VATomatoTip = std::make_shared<VATomatoTip>();
		m_brc1.reset(new AttrClass());
		m_brc1->SetThis(m_brc1);
		m_brc2.reset(new AttrClass());
		m_brc2->SetThis(m_brc2);
	}
	~ScreenAttributes();

	AttrClassPtr m_brc1;
	VATomatoTipPtr m_VATomatoTip;

	void InsertText(ULONG line, ULONG pos, LPCSTR txt);
	void AddDisplayAttribute(EdCntPtr ed, WTString str, long pos, ScreenAttributeEnum flag);
	void QueueDisplayAttribute(EdCntPtr ed, WTString str, long pos, ScreenAttributeEnum flag);

	void ProcessQueue_Underlines();
	void ProcessQueue_AutoReferences();
	void ProcessQueue_Hashtags();
	void CancelAutoRefQueueProcessing()
	{
		mProcessAutoRefQueue = false;
	}
	size_t QueueSize_Underlines() const
	{
		return m_queuedUlVec.size();
	}
	size_t QueueSize_AutoReferences() const
	{
		return m_queuedAutoRefVec.size();
	}
	size_t QueueSize_Hashtags() const
	{
		return m_queuedHashtagsVec.size();
	}
	AttrClassPtr AttrFromPoint(EdCntPtr ed, int x, int y, LPCSTR sym);
	WTString GetSummary() const;
	WTString GetBraceSummary() const;

	void OptionsUpdated();
	void Invalidate(ScreenAttributeEnum flag);
	void Invalidate();
	void InvalidateBraces();

  private:
	void DoAddDisplayAttribute(EdCntPtr ed, WTString str, long pos, ScreenAttributeEnum flag, bool queue);

	typedef std::list<AttrClassPtr> AttrVec;

	void InvalidateVec(AttrVec& vec);
	void InvalidateVec(AttrVec& vec, ScreenAttributeEnum flag);

	mutable CCriticalSection mVecLock;
	AttrVec m_ulVect;
	AttrVec m_AutoRefs;
	AttrVec m_hashtagsVec;
	AttrVec m_refs; // no auto-refs
	AttrVec m_queuedUlVec;
	AttrVec m_queuedAutoRefVec;
	AttrVec m_queuedHashtagsVec;
	volatile bool mProcessAutoRefQueue;
	AttrClassPtr m_brc2;
};

extern ScreenAttributes g_ScreenAttrs;
extern BOOL g_StrFromCursorPosUnderlined;
extern WTString g_StrFromCursorPos;
extern CPoint g_CursorPos;
