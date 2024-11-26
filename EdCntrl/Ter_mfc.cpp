// ter_mfc.cpp : implementation file
//

#include "stdafxed.h"
#include "file.h"
#include "ter_mfc.h"
#include "settings.h"
#include "log.h"
#include "oleobj.h"
#include "..\addin\DSCmds.h"
#include "VaMessages.h"
#include "wtcsym.h"
#include "DevShellAttributes.h"
#include "DevShellService.h"
#include "timer.h"
#include "expansion.h"
#include "FontSettings.h"
#include "VACompletionSet.h"
#include "StringUtils.h"
#include "mainThread.h"
#include "GetFileText.h"
#ifdef RAD_STUDIO
#include "CppBuilder.h"
#include "RadStudioPlugin.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

int g_dopaint = 1;

/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// CTer

IMPLEMENT_DYNAMIC(CTer, CWnd)

BEGIN_MESSAGE_MAP(CTer, CWnd)
//{{AFX_MSG_MAP(CTer)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// Construction
CTer::CTer()
{
#ifdef VA_CPPUNIT
	m_pDoc = nullptr;
#endif
	lp1 = lp2 = 0;
	m_lnx = 1;
	m_posx = 0;
	SetBufState(BUF_STATE_CLEAN);
	m_hasSel = 0;
	m_VSNetDoc = NULL;
	mEolType = EolTypes::eolCrLf;
	mEolTypeChecked = false;
}

CTer::~CTer()
{
	if (m_pDoc)
		delete m_pDoc;

	// [case: 104121]
	try
	{
		DestroyWindow();
	}
	catch (CException* e)
	{
		e->Delete();
	}
}

BOOL CTer::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	return FALSE;
}

// Specify address where mfc should store the address for the window process
WNDPROC* CTer::GetSuperWndProcAddr()
{
	static WNDPROC NEAR pfnSuper;
	return &pfnSuper;
}

// Passed a position, return a line/col
// Position can be either an offset into the file, or an encoded Row/Col
long CTer::PosToLC(const WTString buf, LONG pos, LONG& line, long& col, BOOL expandTabs) const
{
	DEFTIMERNOTE(qqqPosToLC, NULL);
	if (pos == -1)
		pos = buf.GetLength();
	if (TERISRC(pos))
	{
		// simply decode the line/col from pos
		line = (LONG)TERROW(pos);
		col = TERCOL(pos);
	}
	else
	{
		// Count the numbers of lines/cols that appear before pos
		int p = 0;
		line = 1;

		// Use Cached position so we don't need to start from the top
		if (pos > m_posx)
		{
			line = m_lnx;
			p = m_posx;
		}

		if (p > buf.GetLength())
		{
			// ASSERT(FALSE); // line from char 100000 in edcnt::Scope
			p = buf.GetLength(); // just set to eof
		}

		uint tabSz = Psettings->TabSize;
		for (col = 1; p < pos && buf[p]; p++, col++)
		{
			if (buf[p] == '\n' || (buf[p] == '\r' && buf[p + 1] != '\n'))
			{
				line++;
				col = 0;
			}

			if (expandTabs && buf[p] == '\t' && tabSz)
				col = long((col + tabSz - 1) / tabSz * tabSz);
			else if ((buf[p] & 0x80) /*&& buf[p + 1]*/)
			{
				int len = ::GetUtf8SequenceLen(&buf.c_str()[p]);
				if (len)
				{
					if (4 == len)
					{
						// [case: 138734]
						// surrogate pair in utf16.
						// increase col count that we return, to return utf16
						// elements rather than chars.
						// this fixes selection of symbol during alt+g
						++col;
					}

					p += len - 1;
				}
				else
				{
					_ASSERTE(!"PosToLC bad utf8 seq len, breaking infinite loop");
					vLog("ERROR: PosToLC bad utf8 seq len");
					break;
				}
			}
		}
	}
	return TERRCTOLONG(line, col);
}

long CTer::PosToLC(LONG pos, LONG& line, long& col, BOOL expandTabs /*= FALSE*/) const
{
	return PosToLC(GetBufConst(), pos, line, col, expandTabs);
}

// convert from a long LC formatted position to an offset index into buf
long CTer::GetBufIndex(const WTString bufIn, long pos)
{
	long l = 0, c = 0, p = 0;
	DEFTIMERNOTE(qqqGetIdx, NULL);
	LPCSTR buf = bufIn.c_str();
	l = (long)TERROW(pos);
	c = TERCOL(pos);
	if (!TERISRC(pos))
	{
		if ((UINT)pos < (UINT)bufIn.GetLength())
			return pos; // position passed is an offset already
		else
			return bufIn.GetLength();
	}
	// count the number of characters that appear before the L/C
	int cl = 1;
	// Use Cached position so we don't need to start from the top
	if (l > m_lnx)
	{
		cl = (m_lnx);
		p = m_posx;
	}

	if (p > bufIn.GetLength()) // sanity check
	{
		// ASSERT(FALSE);
		m_lnx = 1;
		m_posx = 0;
		p = 0;
	}

	for (; buf[p] && cl < l; p++)
	{
		if (buf[p] == '\n' || (buf[p] == '\r' && buf[p + 1] != '\n'))
		{
			cl++;
			if (cl == l - 100)
			{
				m_posx = p + 1;
				m_lnx = cl;
			}
		}
	}

	// get col
	uint tabSz = Psettings->TabSize;
	int col;
	char prevCh = '\0';
	for (col = 1; buf[p] && col < c; p++, col++)
	{
		if (buf[p] == '\n')
		{
			// [case: 82562]
			// in files whose line endings are \r\n, we return \r at EOL.
			// in files whose line endings are \n, return the \n at EOL for compat with \r\n
			// logic that checks ch for wt_isspace.
			if (prevCh != '\r')
				p++;

			break;
		}

		prevCh = buf[p];
		if (buf[p] == '\t' && tabSz && (!gShellAttr->IsDevenv() || gShellAttr->IsCppBuilder()))
			col = int((col + tabSz - 1) / tabSz * tabSz);
		else if ((buf[p] & 0x80) /*&& buf[p + 1]*/)
		{
			int len = ::GetUtf8SequenceLen(&buf[p]);
			if (len)
			{
				if (4 == len)
				{
					// [case: 138734]
					// surrogate pair in utf16.
					// increase col count that we return, to return utf16
					// elements rather than chars.
					// this fixes scope and bold bracing in lines that contain
					// surrogate pairs (fixes caret position calculation)
					++col;
				}

				p += len - 1;
			}
			else
			{
				_ASSERTE(!"GBI bad utf8 seq len, breaking infinite loop");
				vLog("ERROR: GBI bad utf8 seq len");
				break;
			}
		}
	}

	if (col > 1 && p && col > c) // right of tab
		p--;                     // return position of tab

	return p;
}

// WARNING: Returns "active" pos for both start and end.  "active"
// point is end-of-the-selection (where mouse-up occurred), not
// necessarily the "higher" position.
void CTer::GetSel(long& StartPos, long& EndPos) const
{
	long l, c /*, p = 0*/;
	{
		// get end point
#ifdef TI_BUILD
		CPoint cpt;
		::SendMessage(*this, CC_GET_CURRENT_POS, (WPARAM)&cpt, NULL);
		l = cpt.y;
		c = cpt.x;
#else
		if (!gShellAttr->IsMsdev())
		{
			CPoint cpt1, cpt2;
			static CPoint spt1;
			static CPoint spt2;
			static CPoint scpt;
			SendVamMessage(VAM_GETSEL, (WPARAM)&cpt1, (LPARAM)&cpt2);
			spt1 = cpt1;
			spt2 = cpt2;
			l = cpt2.y;
			c = cpt2.x;
			EndPos = TERRCTOLONG(cpt2.y, cpt2.x);
			StartPos = TERRCTOLONG(cpt1.y, cpt1.x);
			return;
		}
		else
			m_pDoc->CurPos(l, c);
#endif // TI_BUILD
	}
	StartPos = EndPos = TERRCTOLONG(l, c);
	// only get beginning position, if we just set it
	if (StartPos == lp2 && m_hasSel)
		StartPos = lp1;
}

// GetSel2 actually gets both start and end points, unlike GetSel.
// startPos might be > endPos, if selection drag was backwards.
// startPos = anchor pos
// endPos = active pos
void CTer::GetSel2(long& startPos, long& endPos)
{
#if defined(RAD_STUDIO)
	// GetSel gets both as expected in CppBuilder
	return GetSel(startPos, endPos);
#else
	if (gShellAttr->IsMsdev())
	{
		startPos = GetSelBegPos();
		long l, c;
		m_pDoc->CurPos(l, c);
		endPos = TERRCTOLONG(l, c);
	}
	else
	{
		CPoint anchorPt;
		CPoint activePt;
		SendVamMessage(VAM_GETSEL2, (WPARAM)&anchorPt, (LPARAM)&activePt);
		startPos = TERRCTOLONG(anchorPt.y, anchorPt.x);
		endPos = TERRCTOLONG(activePt.y, activePt.x);
	}
#endif
}

// Highlight a block of text.  if nStartChar is 0 and nEndChar is -1,
// the entire text is highlighted.
void CTer::SetSel(long nStartChar, long nEndChar)
{
	vCatLog("Editor.Events", "VaEventED   SetSel  nStartChar=0%lx, '%s'", nStartChar,
	     (const char*)GetSubString((ulong)nStartChar, (ulong)nEndChar).c_str());
	lp1 = nStartChar;
	lp2 = nEndChar;
	long tp1, tp2;
	{
		AutoLockCs l(mDataLock);
		if (!TERISRC(lp1))
			lp1 = PosToLC(m_buf, lp1, tp1, tp2, gShellAttr->IsMsdev() || gShellAttr->IsCppBuilder());
		if (!TERISRC(lp2))
			lp2 = PosToLC(m_buf, lp2, tp1, tp2, gShellAttr->IsMsdev() || gShellAttr->IsCppBuilder());
	}

	{
#ifdef TI_BUILD
		CRect sel(TERCOL(nStartChar), TERROW(nStartChar), TERCOL(nEndChar), TERROW(nEndChar));
		SendMessage(CC_SET_CURRENT_SEL, (WPARAM)&sel, NULL);
#else
		if (!gShellAttr->IsMsdev())
		{
			CPoint pt1((int)TERCOL(lp1), (int)TERROW(lp1));
			CPoint pt2((int)TERCOL(lp2), (int)TERROW(lp2));
			SendVamMessage(VAM_SETSEL, (WPARAM)&pt1, (LPARAM)&pt2);
#if defined(RAD_STUDIO)
			Invalidate();
#endif
			::UpdateWindow(m_hWnd);
		}
		else
		{
			m_pDoc->GoTo((long)TERROW(lp1), (long)TERCOL(lp1), 0);
			m_pDoc->GoTo((long)TERROW(lp2), (long)TERCOL(lp2), 1);
		}
#endif // TI_BUILD
	}
	m_hasSel = (nStartChar != nEndChar);
}

// retrieve the character index of the line.  Set nLine to -1 to specify
// the current line.  The nLine is zero based parameter.
long CTer::LineIndex(long nLine)
{
	if (nLine < 0)
		nLine = LineFromChar(-1);
	return TERRCTOLONG(nLine, 1);
}

// get the line number (0 based) from the character index (0 based).
// when nIndex is -1, the number of the line that contains the first character
// of the selection is returned.  If there is no selection, the current line
// number is returned.
long CTer::LineFromChar(long nIndex)
{
	long l, c;
	if (nIndex < 0)
		GetSel(nIndex, nIndex);
	PosToLC(GetBufConst(), nIndex, l, c);
	return l;
}

// Replace the existing highlighted text by the specified text.  If a block
// is not highlighted, the new text is inserted at the current cursor location.
BOOL CTer::ReplaceSel(const char* NewText, vsFormatOption vsFormat)
{
	BOOL rslt;
	vCatLog("Editor.Events", "VaEventED   Replace '%s', format=%d", NewText, vsFormat);
	if (gShellAttr->IsDevenv())
	{
		_ASSERTE(vsFormat != vsfAutoFormat || gShellAttr->IsDevenv8OrHigher());
		const CStringW txtW(::MbcsToWide(NewText, strlen_i(NewText)));

		if (gShellAttr->IsDevenv15u7OrHigher() && vsFormat & vsfAutoFormat && IsCFile(gTypingDevLang))
		{
			// [case: 115449]
			rslt = ReplaceSelW(txtW, vsFormat);
		}
		else
			rslt = (BOOL)SendVamMessage(VAM_REPLACESELW, (WPARAM)(LPCWSTR)txtW, vsFormat);
	}
	else
	{
		// vsFormat is not applicable here
		_ASSERTE(vsFormat == noFormat && "msdev will not format on replaceSel");
		rslt = m_pDoc->ReplaceSel(NewText);

		// gmit: case 4276 workaround
		if (g_CompletionSet && !g_CompletionSet->IsExpUp(NULL))
		{
			CRect rect;
			GetWindowRect(rect);
			SendMessage(WM_SIZE, 0, MAKELPARAM(rect.Width(), rect.Height()));
		}
	}
	GetSel(lp1, lp2);
	// make sure lp1, lp2 is up to date.

	return rslt;
}

BOOL CTer::ReplaceSelW(const CStringW& NewText, vsFormatOption vsFormat)
{
	BOOL rslt;

	vCatLog("Editor.Events", "VaEventED   Replace '%s', format=%d", (LPCSTR)CString(NewText), vsFormat);
	if (gShellAttr->IsDevenv())
	{
#if !defined(RAD_STUDIO)
		_ASSERTE(vsFormat != vsfAutoFormat || gShellAttr->IsDevenv8OrHigher());
#endif

#if 0 // fixed in 15.7 preview 4
		if (gShellAttr->IsDevenv15u7OrHigher() &&
			vsFormat & vsfAutoFormat &&
			IsCFile(gTypingDevLang))
		{
			// [case: 115449]

			// save starting selection
			long p1 = 0, p2 = 0;
			GetSel2(p1, p2);
			if (p1 > p2)
				std::swap(p1, p2);

			// insert text
			rslt = SendVamMessage(VAM_REPLACESELW, (WPARAM)(LPCWSTR)NewText, vsFormat);

			// get new selection -- which is just a caret position, not a selection
			long p1a = 0, p2a = 0;
			GetSel2(p1a, p2a);
			if (p1a > p2a)
				std::swap(p1a, p2a);

			// create a selection using starting position and post-insert position
			SetSel(p1, p2a);

			// format the new selection
			gShellSvc->FormatSelection();

			// get the new formatted selection
			GetSel2(p1a, p2a);
			if (p1a > p2a)
				std::swap(p1a, p2a);

			// restore the old post-insert position relative to the formatted text
			SetSel(p2a, p2a);
		}
		else
#endif
		rslt = (BOOL)SendVamMessage(VAM_REPLACESELW, (WPARAM)(LPCWSTR)NewText, vsFormat);

		GetSel(lp1, lp2); // from ReplaceSel
	}
	else
	{
		WTString txt(NewText);
		rslt = ReplaceSel(txt.c_str(), vsFormat);
	}

	return rslt;
}

void CTer::BufRemove(long p1, long p2)
{
	WTString curBuf(GetBufConst());
	long offset1 = GetBufIndex(curBuf, min(p1, p2));
	long offset2 = GetBufIndex(curBuf, max(p1, p2));
	int len = min(curBuf.GetLength() - offset2, 1000);
	LPSTR buf = curBuf.GetBuffer(0);
	_tcsncpy(&buf[offset1], &buf[offset2], (size_t)len);
	curBuf.ReleaseBuffer();

	AutoLockCs l(mDataLock);
	m_buf = curBuf;
}

// Get at string based on a start/stop
WTString CTer::GetSubString(ulong p1, ulong p2)
{
	LOG("CTer__GetSubString");
	const WTString curBuf(GetBufConst());
	long idx1 = GetBufIndex(curBuf, (long)p1);
	long idx2 = GetBufIndex(curBuf, (long)p2);
	WTString wstr = curBuf.Mid((size_t)min(idx1, idx2), (size_t)abs(idx2 - idx1));
	return wstr;
}

int GetColumnOffset(LPCSTR line, int pos)
{
	if (pos < 1)
		return 0;
	LPCSTR p = line;
	const uint tabSz = Psettings->TabSize ? Psettings->TabSize : 4u;
	int yPos = 0;
	for (int i = 0; i < pos && p[i]; i++)
	{
		char c = p[i];
		if (c == '\t')
		{
			int tabStop = 0;
			if (g_vaCharWidth[' '] > 0)
				for (tabStop = 0; tabStop <= yPos; tabStop += (tabSz * g_vaCharWidth[' ']))
					;
			yPos = tabStop;
		}
		else if (c & 0x80)
		{
			int len = ::GetUtf8SequenceLen(&p[i]);
			if (len)
			{
				// this does not need special handling for surrogate pairs
				i += len - 1;
			}
			else
			{
				_ASSERTE(!"GetColOff bad utf8 seq len, breaking infinite loop");
				vLog("ERROR: GetColOff bad utf8 seq len");
				break;
			}
			yPos += g_vaCharWidth['W'];
		}
		else if (c > 0 && c < 255)
			yPos += g_vaCharWidth[c];
		else
			yPos += g_vaCharWidth['W'];
	}
	return yPos;
}

// Edcnt keeps a buffer of the text in the editor.
// As the user types, we update the buffer to minimize
// calls to reget the buffer.  Once we see see the file is
// modified, we mark our buffer as dirty and set a timer to
// reload it once the system goes idle.
WTString CTer::GetBuf(BOOL get)
{
	WTString curBuf(GetBufConst());
	if (get || !curBuf.GetLength())
	{
		DEFTIMERNOTE(GetBuf, NULL);
		CWnd* foc = vGetFocus();
		// if get, we force the read, so if the file is opened from pressing a button,
		// we can still get the text and parse the file.
		if (!get && ((GetKeyState(VK_LBUTTON) & 0x1000)))
			// ok if lbutton is down and listbox is up because we are selecting from listbox
			return curBuf;

		if (!foc)
		{
			if (g_mainThread != ::GetCurrentThreadId())
			{
				// [case: 57243] m_buf empty when unsaved file does not have focus?
				AutoLockCs l(mDataLock);
				if (curBuf.IsEmpty() && !mPreviousBuf.IsEmpty())
					return mPreviousBuf;
			}
			return curBuf;
		}

		// make sure we we get the text from our edcnt
		EdCnt* focusedEd = (EdCnt*)foc->SendMessage(WM_VA_GET_EDCNTRL, 0, 0);
		if (focusedEd)
		{
			if (foc != this || focusedEd != this)
				return curBuf;
		}
		else
		{
			// no edcnt from focus window and currentEdCntrl is not
			// this - don't hose our buf. (case=10432)
			if (g_currentEdCnt && g_currentEdCnt.get() != this)
				return curBuf;
		}

		m_lnx = 1;
		m_posx = 0;
		{
			AutoLockCs l(mDataLock);
			mPreviousBuf = curBuf;
		}
		const WTString prevBuf(curBuf);

		if (gShellAttr->IsDevenv())
		{
			curBuf.Empty();
#if defined(RAD_STUDIO)
			CStringW widestr(::PkgGetFileTextW(static_cast<EdCnt*>(this)->FileName()));
#else
			CStringW widestr(::PkgGetFileTextW(static_cast<EdCnt*>(this)->m_IVsTextView));
#endif
			if (widestr.IsEmpty())
			{
				// [case: 53350]
				DWORD bufLen = 0;
				LPCWSTR pCurBuf = (LPCWSTR)SendVamMessage(VAM_GETTEXTW, (WPARAM)&bufLen, NULL);
				widestr = CStringW(pCurBuf, (int)bufLen);
			}

			if (!widestr.IsEmpty())
				curBuf = ::WideToMbcs(widestr, widestr.GetLength()) + kExtraBlankLines;

			AutoLockCs l(mDataLock);
			m_buf = curBuf;
		}
		else
		{
			TerNoScroll dontscroll(this);
			long r, c;
			m_pDoc->CurPos(r, c);
			::SendMessage(gVaMainWnd->GetSafeHwnd(), WM_COMMAND, DSM_SWAPANCHOR, 0);
			long r1, c1;
			m_pDoc->CurPos(r1, c1);
			curBuf = m_pDoc->GetText() + kExtraBlankLines;
			{
				AutoLockCs l(mDataLock);
				m_buf = curBuf;
			}
			m_pDoc->GoTo(r1, c1);
			m_pDoc->m_begSel = TERRCTOLONG(r1, c1);
			m_pDoc->GoTo(r, c, TRUE);
			long r2, c2;
			m_pDoc->CurPos(r2, c2);
			if (r == r2 && c2 == 1 && c != 1)
			{
				// lost indentation
				gShellSvc->FormatSelection();
				curBuf = GetBufConst();
			}
		}

		// [case: 95608]
		// check when both increasing and decreasing since eoltype change can
		// reduce length (as well as undo or redo depending on direction of change)
		// #checkEolType
		if (!mEolTypeChecked || ::abs(curBuf.GetLength() - prevBuf.GetLength()) > 15)
		{
			if (curBuf.GetLength() != kExtraBlankLines.GetLength())
			{
				auto eol = EolTypes::GetEolType(curBuf);
				if (eol != mEolType)
					mEolType = eol;
				mEolTypeChecked = true;
			}
		}
		SetBufState(BUF_STATE_CLEAN);
		return GetBufConst();
	}

	return curBuf;
}

// This is a quick stab at making our buffer similar
// to what is on the users screen.  Mark buffer dirty and reload
// on next idle state.
long CTer::BufInsert(long pos, LPCSTR str)
{
	int slen = strlen_i(str);
	WTString curBuf;
	{
		AutoLockCs l(mDataLock);
		mPreviousBuf = curBuf = m_buf;
	}
	LPSTR buf = curBuf.GetBuffer(0);
	WTString tstr;
	int blen = curBuf.GetLength() - 10;
	if (m_pDoc && (slen <= 1 && pos > 0 && !(ISCSYM(buf[pos]) && ISCSYM(buf[pos - 1])) && !strchr(str, '\n')))
	{
		TerNoScroll dontscroll(this);
		int tpos = GetBufIndex(curBuf, LineIndex());
		// for(;pos && buf[pos-1] != '\n'; pos--);
		long p1, p2;
		GetSel(p1, p2);
		m_pDoc->SelLine();
		tstr = m_pDoc->GetCurSelection();
		if (tstr.Find('\n') == -1)
			tstr.append("\r\n");
		SetSel(p1, p2);
		int llen;
		for (llen = 0; buf[tpos + llen] && buf[tpos + llen] != '\n'; llen++)
			;
		if ((slen + llen + 1) != tstr.GetLength() || strncmp(&buf[tpos], tstr.c_str(), (size_t)(pos - tpos)) != 0)
		{
			// line does not match what we think it should be
			// insert their line...
			str = tstr.c_str();
			pos = tpos;
			slen = tstr.GetLength();
		}
	}
	// quick stab, don't shift whole buffer.
	if ((blen - pos) > 1000)
		blen = pos + 1000;
	// shift characters so we can insert text
	if (blen > slen) // make sure there is enough room for the insert
		for (int i = blen - 1; i > pos && i >= slen; i--)
			buf[i] = buf[i - slen];

	// insert the text...
	for (int i = 0; (i + pos) < blen && i < slen; i++)
		buf[i + pos] = str[i];

	curBuf.ReleaseBuffer(-1);
	{
		AutoLockCs l(mDataLock);
		m_buf = curBuf;
	}
	SetBufState(BUF_STATE_DIRTY); // buffer is not exact
	return slen;
}

BOOL CTer::HasSelection()
{
#ifdef TI_BUILD
	return FALSE;
#else
	if (gShellAttr->IsDevenv())
	{
		const CStringW sel((LPCWSTR)SendVamMessage(VAM_GETSELSTRINGW, 0, 0));
		m_hasSel = sel.GetLength() != 0;
		return m_hasSel;
	}
	else
	{
		bool hadsel = m_hasSel;
		m_hasSel = CTer::m_pDoc->GetCurSelection().GetLength() != 0;
		if (m_hasSel && !hadsel)
			Invalidate(TRUE);
		return m_hasSel;
	}
#endif // TI_BUILD
}

TerNoScroll::TerNoScroll(CTer* pTer)
{
	LOG("TerNoScroll()");
	if (gShellAttr->IsDevenv())
		return;
	ASSERT(gShellAttr->IsMsdev() && pTer->m_pDoc);
	m_pTer = pTer;
	g_dopaint = 0;
	mCaretPos = m_pTer->vGetCaretPos();
	long r, c;
	m_pTer->m_pDoc->CurPos(r, c);
	p2 = TERRCTOLONG(r, c);

	CWnd* pwnd = m_pTer->GetWindow(GW_HWNDFIRST);
	//	ASSERT(pwnd && pwnd->GetDlgCtrlID() == 0xea00);
	_ASSERTE(g_FontSettings->CharSizeOk());
	while (pwnd)
	{
		// if( pwnd->GetDlgCtrlID() > 1)
		pwnd->SetRedraw(FALSE);
		pwnd = pwnd->GetWindow(GW_HWNDNEXT);
	}

	::SendMessage(m_pTer->m_hWnd, WM_COMMAND, DSM_SWAPANCHOR, 0);
	m_pTer->m_pDoc->CurPos(r, c);
	m_pTer->m_pDoc->m_begSel = TERRCTOLONG(r, c);
	p1 = TERRCTOLONG(r, c);
}

TerNoScroll::~TerNoScroll()
{
	LOG("~TerNoScroll()");
	if (gShellAttr->IsDevenv())
		return;

	m_pTer->SetSel(p1, p2);
	CPoint pt = m_pTer->vGetCaretPos();
	CPoint lp = mCaretPos;
	for (; pt != lp && pt.y < mCaretPos.y;)
	{
		lp = pt;
		m_pTer->SendMessage(WM_VSCROLL, SB_LINEUP, (LPARAM)NULL);
		// this is an f'd up way to make them update the caret pos -Jer
		m_pTer->SendMessage(WM_HSCROLL, SB_LINELEFT, (LPARAM)NULL);
		m_pTer->SendMessage(WM_HSCROLL, SB_LINERIGHT, (LPARAM)NULL);
		pt = m_pTer->vGetCaretPos();
	}
	lp = mCaretPos;
	for (; pt != lp && pt.y > mCaretPos.y;)
	{
		lp = pt;
		m_pTer->SendMessage(WM_VSCROLL, SB_LINEDOWN, (LPARAM)NULL);
		// this is an f'd up way to make them update the caret pos -Jer
		m_pTer->SendMessage(WM_HSCROLL, SB_LINELEFT, (LPARAM)NULL);
		m_pTer->SendMessage(WM_HSCROLL, SB_LINERIGHT, (LPARAM)NULL);
		pt = m_pTer->vGetCaretPos();
	}
	lp.x = -1;
	while (pt != lp && mCaretPos.x > 0 && pt.x > mCaretPos.x)
	{
		lp = pt;
		m_pTer->SendMessage(WM_HSCROLL, SB_LINERIGHT, (LPARAM)NULL);
		pt = m_pTer->vGetCaretPos();
	}
	if (!Psettings->m_borderWidth && !pt.x)
	{
		// to offset the rscroll above, because get getcaretpos lies
		m_pTer->SendMessage(WM_HSCROLL, SB_LINELEFT, (LPARAM)NULL);
	}

	CPoint lpt(0, 0);
	while (pt.x < mCaretPos.x && pt != lpt)
	{
		lpt = pt;
		m_pTer->SendMessage(WM_HSCROLL, SB_LINELEFT, (LPARAM)NULL);
		pt = m_pTer->vGetCaretPos();
	}
	CWnd* pwnd = m_pTer->GetWindow(GW_HWNDFIRST);
	while (pwnd)
	{
		pwnd->SetRedraw(TRUE);
		pwnd = pwnd->GetWindow(GW_HWNDNEXT);
	}
	g_dopaint = 1;

	if (g_loggingEnabled > 2 && mCaretPos != m_pTer->vGetCaretPos())
	{
		// debug logging for a user having problems with screen shift
		char buf[100];
		CPoint pt2 = m_pTer->vGetCaretPos();
		sprintf(buf, "cpos(%ld, %ld) != caretpos(%ld, %ld)", mCaretPos.x, mCaretPos.y, pt2.x, pt2.y);
		Log(buf);
	}

	// invalidate caretpos to reduce ghost carets
	CRect r(mCaretPos.x, mCaretPos.y, mCaretPos.x + g_FontSettings->GetCharWidth(),
	        mCaretPos.y + g_FontSettings->GetCharHeight() + 2);
	m_pTer->InvalidateRect(&r, FALSE);
	m_pTer->Invalidate(FALSE);
}

void TerNoScroll::OffsetLine(int lines)
{
	_ASSERTE(p1 == p2);
	int orgLine = (int)TERROW((size_t)p1);
	int orgCol = TERCOL(p1);
	orgLine += lines;
	p1 = p2 = TERRCTOLONG(orgLine, orgCol);
}

void CTer::UpdateBuf(const WTString& buf, bool checkEol /*= true*/)
{
	WTString prevBuf;
	{
		AutoLockCs l(mDataLock);
		// [case: 53874]
		prevBuf = mPreviousBuf = m_buf;
		m_buf = buf;
	}

	if (checkEol)
	{
		// [case: 95608]
		// since m_buf is getting updated here, need to check eoltype like GetBuf does.
		// check when both increasing and decreasing since eoltype change can
		// reduce length (as well as undo or redo depending on direction of change)
		// #checkEolType
		if (::abs(buf.GetLength() - prevBuf.GetLength()) > 15)
		{
			auto eol = EolTypes::GetEolType(buf);
			if (eol != mEolType)
				mEolType = eol;
		}
	}
}

void CTer::ClearBuf()
{
	AutoLockCs l(mDataLock);
	m_buf.Empty();
	mPreviousBuf.Empty();
	m_bufState = CTer::BUF_STATE_CLEAN;
}

long CTer::GetSelBegPos()
{
	if (!gShellAttr->IsMsdev())
	{
		const WTString sel((LPCWSTR)SendVamMessage(VAM_GETSELSTRINGW, 0, 0));
		GetBuf(TRUE);
		int slen = sel.GetLength();
		long p1, p2;
		GetSel(p1, p2);
		long idx = GetBufIndex(p1);
		if (idx >= slen)
		{
			// fix for #in template broken at line 0 col 0
			AutoLockCs l(mDataLock);
			LPCSTR p = m_buf.c_str();
			if (strncmp(&p[idx - slen], sel.c_str(), (size_t)slen) == 0)
				p1 = idx - slen;
			else
				p1 = p1;
		}
		return p1;
	}
	else
	{
		WTString sel = m_pDoc->GetCurSelection();
		if (!sel.GetLength())
		{
			long p1, p2;
			GetSel(p1, p2);
			return p2;
		}
		else
		{
			{
				TerNoScroll noscroll(this);
			}
			return m_pDoc->m_begSel;
		}
	}
}

void CTer::SwapAnchor()
{
	gShellSvc->SwapAnchor();
}

void CTer::SetRedraw(BOOL bRedraw /*= TRUE*/)
{
	// Do we need to change redrawing?
	static BOOL redrawing = TRUE;
	redrawing = bRedraw;
}

WTString CTer::GetLineBreakString()
{
	GetBuf(m_bufState != BUF_STATE_CLEAN);
	return EolTypes::GetEolStr(mEolType);
}

void CTer::SetBufState(int state)
{
	if (state == BUF_STATE_CLEAN || state > m_bufState)
		m_bufState = state;
}

void CTer::SetIvsTextView(IVsTextView* pView)
{
	if (!m_IVsTextView || pView != m_IVsTextView)
		m_IVsTextView = pView;
}

#ifdef RAD_STUDIO

LRESULT ConvertMessageToInterfaceCall(EdCnt *ed, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (!gRadStudioHost)
		return E_UNEXPECTED;

	auto getSelRange = [ed](auto& startLine, auto& startCol, auto& endLine, auto& endCol) {
		long ss, se;
		if (ed && VaRSSelectionOverride::GetSel(ed, ss, se))
		{
			long line, col;
			ed->PosToLC(ss, line, col, FALSE);
			startLine = (decltype(startLine))line;
			startCol = (decltype(startCol))col;
			ed->PosToLC(se, line, col, FALSE);
			endLine = (decltype(endLine))line;
			endCol = (decltype(endCol))col;
			return 1;
		}
		else
		{
			int sl, si, el, ei;
			if (gRadStudioHost->ExGetSelectionRangeForVA(ed, &sl, &si, &el, &ei))
			{
				startLine = (decltype(startLine))sl;
				startCol = (decltype(startCol))si;
				endLine = (decltype(endLine))el;
				endCol = (decltype(endCol))ei;
				return 2;
			}
		}
		return 0;
	};

	switch (msg)
	{
	case VAM_GETTEXTW: {
		static CStringW buf;

		WTString wtBuf;
		auto fileName = ed->FileName();
		if (RadUtils::TryGetFileContent(fileName, wtBuf))
			buf = wtBuf.Wide();
		else
			buf = gRadStudioHost->ExGetActiveViewText((int*)wparam);
		return (LRESULT)(LPCWSTR)buf;
	}
	break;
	case VAM_GETSELSTRINGW: {
		static CStringW buf;

		int sl, sc, el, ec;
		auto fileName = ed->FileName();
		gRadStudioHost->GetActiveViewSelectionRange(fileName, &sl, &sc, &el, &ec);
		int size = gRadStudioHost->TextRangeSize(fileName, sl, sc, el, ec);
		std::vector<CHAR> buff;
		buff.resize((size_t)size + 5);

		size = gRadStudioHost->GetActiveViewText(fileName, &buff[0], (int)buff.size(), sl, sc, el, ec);
		buff[(size_t)size] = '\0';
		WTString wt = &buff[0];
		buf = wt.Wide();
		    // 		if (getSelRange(sl, sc, el, ec))
// 		{
// 			auto curBuf = ed->GetBuf(TRUE);
// 			long idx1 = ed->GetBufIndex(curBuf, TERRCTOLONG(sl, sc));
// 			long idx2 = ed->GetBufIndex(curBuf, TERRCTOLONG(el, ec));
// 
// 			WTString wstr = curBuf.Mid((size_t)min(idx1, idx2), (size_t)abs(idx2 - idx1));
// 			VADEBUGPRINT("#VAM GetSelString: " << sl << ", " << sc << ", " << el << ", " << ec << " " << wstr.c_str());
// 			buf = wstr.Wide();
// 		}
// 		else
// 		{
// 			buf.Empty();		
// 		}

		return (LRESULT)(LPCWSTR)buf;
	}
	break;
	case VAM_REPLACESELW: {
		int sl, sc, el, ec;
		if (getSelRange(sl, sc, el, ec))
		{		
			VADEBUGPRINT("#VAM Replace: " << sl << ", " << sc << ", " << el << ", " << ec << " " << (LPCWSTR)wparam);

#ifdef _DEBUG
			auto buff = ed->GetBuf(TRUE).Wide();
#else
			ed->GetBuf(TRUE);
#endif // _DEBUG

			{
				RadWriteOperation wop(ed->FileName());
				wop.ReplaceTextVA(sl, sc, el, ec, (WCHAR*)wparam); // lparam is unused, it is "reformat"
			}

#ifdef _DEBUG
			auto newBuff = ed->GetBuf(TRUE).Wide();

			auto areEqual = newBuff.Compare(buff) == 0;

			_ASSERTE(!areEqual);
		
			return (LRESULT)(areEqual ? FALSE : TRUE);
#else
			return (LRESULT)TRUE;
#endif
		}
		break;
	}
	case VAM_GETSEL: {
		POINT *p1 = (POINT*)wparam, *p2 = (POINT*)lparam;
		if (!getSelRange(p1->y, p1->x, p2->y, p2->x))
		{
			p1->x = p1->y = p2->x = p2->y = 1;
		}
		return (LRESULT)TRUE;
	}
	break;
	case VAM_SETSEL: {
		POINT *p1 = (POINT*)wparam, *p2 = (POINT*)lparam;
		LC_VA_2_RAD(&p1->y, &p1->x, &p2->y, &p2->x);
		gRadStudioHost->ExSetActiveViewSelection(p1->y, p1->x, p2->y, p2->x);
		int sl, sc, el, ec;
		if (getSelRange(sl, sc, el, ec) && p1->y == sl && p1->x == sc && p2->y == el && p2->y == ec)
		{
			return (LRESULT)TRUE;
		}
	}
	break;
	case VAM_FILEPATHNAMEW: {
		return (LRESULT)gRadStudioHost->ExGetActiveViewFilename();
	}
	break;
	case WM_VA_FILEOPENW: {
		gRadStudioHost->LoadFileAtLocation((WCHAR*)lparam, (int)wparam, 0, 0, 0);
		return (LRESULT)TRUE;
	}
		break;
	case VAM_SWAPANCHOR:
		    gRadStudioHost->ExSwapAnchorInActiveView();
		break;
	case VAM_GETSELECTIONMODE:
		if (gRadStudioHost->ExActiveViewHasBlockOrMultiSelect())
			return 11;
		break;
	case VAM_EXECUTECOMMAND: {
		const WTString cmdText((const char*)wparam);
		if (cmdText == "Edit.FormatSelection")
			gRadStudioHost->ExFormatActiveViewSelectedText();
		else if (cmdText == "Edit.SelectAll")
			gRadStudioHost->ExSelectAllInActiveView();
		else if (cmdText == "Edit.ScrollLineTop")
			gRadStudioHost->ExScrollLineToTopInActiveView();
		else if (cmdText == "VAssistX.FindReferencesResults")
		{

		}
		else
		{
			_ASSERTE(!"Ignored VAM_EXECUTECOMMAND message in C++Builder");
			vLog("WARN: CPPB: VAM_EXECUTECOMMAND message Ignored %zx %zx", (uintptr_t)wparam, (uintptr_t)lparam);
		}
	}
	break;
	default:
		// if this assert fires, need to add an explicit case for the message and then either ignore or handle it
		_ASSERTE(!"Ignored message in C++Builder");
		vLog("WARN: CPPB: message Ignored %x %zx %zx", msg, (uintptr_t)wparam, (uintptr_t)lparam);
		break;
	}

	return 0;
}

#endif

LRESULT
CTer::SendVamMessage(UINT msg, WPARAM wparam, LPARAM lparam) const
{
#ifdef RAD_STUDIO
	auto ed = GetOpenEditWnd(m_hWnd);
	return ::ConvertMessageToInterfaceCall(ed.get(), msg, wparam, lparam);
#else
	MSG m = {NULL, (UINT)msg, wparam, lparam, NULL, {0, 0}};
	return ::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_CMDWithCURDOCOBJ, (WPARAM)m_VSNetDoc, (LPARAM)&m);
#endif
}

LRESULT
SendVamMessageToCurEd(UINT msg, WPARAM wparam, LPARAM lparam)
{
#ifdef RAD_STUDIO
	auto ed = g_currentEdCnt;
	return ::ConvertMessageToInterfaceCall(ed.get(), msg, wparam, lparam);
#else
	if (g_currentEdCnt)
		return g_currentEdCnt->SendVamMessage(msg, wparam, lparam);
	else if (VAM_EXECUTECOMMAND == msg) // [case: 71069]
		return ::SendMessage(gVaMainWnd->GetSafeHwnd(), msg, wparam, lparam);

	_ASSERTE(!"SendVamMessage and g_currentEdCnt == NULL");
	vLog("ERROR: Message Ignored %x %zx %zx", msg, (uintptr_t)wparam, (uintptr_t)lparam);
	return NULL;
#endif
}
