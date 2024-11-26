#pragma once
// ter_mfc.h : header file
//

#include "EolTypes.h"
#include "vsFormatOption.h"
#include "WTString.h"
#include "VSIP\8.0\VisualStudioIntegration\Common\Inc\textmgr.h"
#include "IEdCnt.h"
#include "Lock.h"

class DocClass;

/////////////////////////////////////////////////////////////////////////////
// CTer window
class WTString;

class CTer : public CWnd, public IEdCnt
{
	DECLARE_DYNCREATE(CTer)
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
	friend struct RunningDocumentData;
#endif

	// non-implemented overrides of non-virtual CWnd methods to make sure we're
	// not calling them.  CWnd versions could still be called if obj is cast CWnd
  private:
	void GetClientRect(LPRECT lpRect) const;
	void ClientToScreen(LPPOINT lpPoint) const;
	void ClientToScreen(LPRECT lpRect) const;
	void ScreenToClient(LPPOINT lpPoint) const;
	void ScreenToClient(LPRECT lpRect) const;
	CWnd* GetFocus() const;
	CWnd* SetFocus();
	CPoint GetCaretPos() const;

  public:
	void* m_VSNetDoc;
	DocClass* m_pDoc;
	CComPtr<IVsTextView> m_IVsTextView;
	virtual void SetIvsTextView(IVsTextView* pView);
	long lp1, lp2; // Warning these values are bogus most of the time in VS.NET
	long m_lnx;
	long m_posx;
	bool m_hasSel;
	int m_firstVisibleLine;
	enum
	{
		BUF_STATE_CLEAN = 0, // should match exactly what the editor has
		BUF_STATE_DIRTY = 1, // Set in BufInsert, where the buffer is the same only up to the current position
		BUF_STATE_WRONG = 2
	}; // Buffer might be totally wrong, after paste/macro/...
	int m_bufState;
	CTer();
	virtual BOOL Create(DWORD, const RECT&, CWnd*, UINT);

  private:
	using CWnd::Create;

  public:
	virtual ~CTer();

	// WARNING: Returns "active" pos for both start and end.  "active"
	// point is end-of-the-selection (where mouse-up occurred), not
	// necessarily the "higher" position.
	void GetSel(long& StartPos, long& EndPos) const;

	// GetSel2 actually gets both start and end points, unlike GetSel.
	// startPos might be > endPos, if selection drag was backwards.
	// startPos = anchor pos
	// endPos = active pos
	void GetSel2(long& startPos, long& endPos);

	long LineIndex(long nLine = -1);

	long LineFromChar(long nIndex = -1);
	BOOL ReplaceSel(const char* NewText, vsFormatOption vsFormat);
	BOOL ReplaceSelW(const CStringW& NewText, vsFormatOption vsFormat);
	void SetSel(long nStartChar, long nEndChar);

	BOOL HasSelection();
	BOOL IsRSelection() const
	{
		return FALSE;
	}
	WTString GetSubString(ulong p1, ulong p2);

	long GetBufIndex(long pos)
	{
		return GetBufIndex(GetBufConst(), pos);
	}
	long GetBufIndex(const WTString buf, long pos);
	long PosToLC(const WTString buf, LONG pos, LONG& line, long& col, BOOL expandTabs = FALSE) const;
	long PosToLC(LONG pos, LONG& line, long& col, BOOL expandTabs = FALSE) const;

	long BufInsert(long pos, LPCSTR str);
	void BufRemove(long p1, long p2);

	void SetBufState(int state);
	WTString GetBuf(BOOL force = FALSE);
	WTString GetBufConst() const
	{
		AutoLockCs l(mDataLock);
		return m_buf;
	}
	bool IsBufEmpty() const
	{
		AutoLockCs l(mDataLock);
		return m_buf.IsEmpty();
	}
	void UpdateBuf(const WTString& buf, bool checkEol = true);
	void ClearBuf();
	long GetSelBegPos();
	void SwapAnchor();

	// Do we really need to disable redrawing?
	void SetRedraw(BOOL bRedraw = TRUE);
	WTString GetLineBreakString();
	virtual void ReserveSpace(int top, int left, int height, int width)
	{
	} // Stub for vs2010 ISpaceReservationAgent
	virtual bool IsQuickInfoActive()
	{
		return false;
	}
	LRESULT SendVamMessage(UINT msg, WPARAM wparam, LPARAM lparam) const;
	virtual void ResetZoomFactor()
	{
	}

  public:
	WNDPROC* GetSuperWndProcAddr();

  protected:
	DECLARE_MESSAGE_MAP()

	EolTypes::EolType mEolType;
	bool mEolTypeChecked;

	mutable CCriticalSection mDataLock; // lock for m_pmparse, mPendingMp, filename, m_buf

  private:
	// mPreviousBuf is write-only.
	// Before modifying m_buf, copy to mPreviousBuf.
	WTString mPreviousBuf;		// protected by mDataLock
	WTString m_buf;				// protected by mDataLock, access via GetBuf, GetBufConst
};

#define DSNOSCROLL TerScroll this;
class TerNoScroll
{
	CTer* m_pTer;
	long p1, p2;
	CPoint mCaretPos;

  public:
	TerNoScroll(CTer* pTer);
	~TerNoScroll();

	void OffsetLine(int lines);
};
LRESULT SendVamMessageToCurEd(UINT msg, WPARAM wparam, LPARAM lparam);
// Chris Price - July 2, 2000 - Messages needed to implement support for Visual Assist
// See Visual_Assist.doc for further documentation.
#define CC_GET_LENGTH 30000
#define CC_GET_TEXT 30001
#define CC_GET_CURRENT_POS 30002
#define CC_GET_CURRENT_SEL 30003
#define CC_SCREEN_TO_CHAR_OFFSET 30004
#define CC_CHAR_OFFSET_TO_SCREEN 30005
#define CC_REPLACE_TEXT 30006
#define CC_SET_CURRENT_SEL 30007
#define CC_GET_MODIFIED_COUNT 30008
// end CP

// Client's editor can be row/col based or absolute position into file.
// Edcnt uses longs to keep track of both r/c's and absolute positions
// The long will be in the format 0-0x0ffffffff to signify an abs position
// or 0x6RRRRCCC to signify a row col format

// increased RRRR to 1RRRR to move the 65K limit to 131k lines
//	bits [011r rrrr rrrr rrrr rrrr cccc cccc cccc]
#define TERROW(pos) ((ULONG)(((DWORD)(pos) >> 12) & 0x1FFFF))
#define TERCOL(pos) ((WORD)(((DWORD)(pos)) & 0x0FFF))
#define TERRCTOLONG(r, c) (0x60000000 | ((long)r << 12) | c)
#define TERISRC(pos) ((pos & 0xE0000000) == 0x60000000)

/////////////////////////////////////////////////////////////////////////////
