#include "stdafxed.h"
#include <objbase.h>
#include <initguid.h>
#include "oleobj.h"
#include "log.h"
#include "timer.h"
#include "settings.h"
#include "mainThread.h"
#include "wt_stdlib.h"
#include "WTComBSTR.h"
#include "Project.h"

#if _MSC_VER <= 1200
#include <objmodel/textguid.h>
#include <objmodel/textauto.h>
#else
#include "../../../3rdParty/Vc6ObjModel/textguid.h"
#include "../../../3rdParty/Vc6ObjModel/textauto.h"
#endif
#include "StringUtils.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if defined(SEAN)
#define Trap(msg, cmd)                                                                                                 \
	if (cmd)                                                                                                           \
	{                                                                                                                  \
		Log((WTString("OleObj Failed: ") + msg).c_str());                                                              \
		ASSERT(FALSE);                                                                                                 \
		return NULL;                                                                                                   \
	}                                                                                                                  \
	else                                                                                                               \
	{                                                                                                                  \
	};
#define Trap2(msg, cmd)                                                                                                \
	if (cmd)                                                                                                           \
	{                                                                                                                  \
		Log((WTString("OleObj Failed: ") + msg).c_str());                                                              \
		ASSERT(FALSE);                                                                                                 \
		return NULL;                                                                                                   \
	}                                                                                                                  \
	else                                                                                                               \
	{                                                                                                                  \
	};
#else
#define Trap(msg, cmd)                                                                                                 \
	if (cmd)                                                                                                           \
	{                                                                                                                  \
		Log((WTString("OleObj Failed: ") + msg).c_str());                                                              \
		ASSERT(FALSE);                                                                                                 \
		return NULL;                                                                                                   \
	}                                                                                                                  \
	else                                                                                                               \
	{                                                                                                                  \
		Log2(msg);                                                                                                     \
	};
#define Trap2(msg, cmd)                                                                                                \
	if (cmd)                                                                                                           \
	{                                                                                                                  \
		Log((WTString("OleObj Failed: ") + msg).c_str());                                                              \
		ASSERT(FALSE);                                                                                                 \
		return NULL;                                                                                                   \
	}                                                                                                                  \
	else                                                                                                               \
	{                                                                                                                  \
		Log2(msg);                                                                                                     \
	};
#endif
#define Trap3(f)                                                                                                       \
	{                                                                                                                  \
		if (f)                                                                                                         \
		{                                                                                                              \
			Log(WTString("OleObj Failed: trap3").c_str());                                                             \
			ASSERT(FALSE);                                                                                             \
		}                                                                                                              \
	}

DocClass::DocClass(ITextDocument* pDoc)
{
	Log("DocClass::DocClass");
	m_sel = NULL;
	m_disp = NULL;
	m_txtDoc = pDoc;
	if (m_txtDoc) // Needed for CppBuilder
		m_txtDoc->AddRef();
	m_line = m_col = 1;
	m_begSel = 0;
}

DocClass::~DocClass()
{
	UnInit(true);
	Log("DocClass::~DocClass");
}

static BOOL hasOutPut = FALSE;

bool DocClass::GetSelObj()
{
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::GSO gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		if (!m_sel)
			_ASSERTE(!"DocClass::GetSelObj unsafe thread access and no selection object");
		return false;
	}

	UnInit();
	Trap2("QrySel", FAILED(m_txtDoc->get_Selection(&m_disp)));
	Trap2("QrySel", FAILED(m_disp->QueryInterface(IID_ITextSelection, (void**)&m_sel)));
	return true;
}

void DocClass::UnInit(bool all /* = false */)
{
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::UI gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(!"unsafe thread access");
		return;
	}
	try
	{
		if (m_sel)
		{
			m_sel->Release();
			m_sel = NULL;
		}
		if (m_disp)
		{
			m_disp->Release();
			m_disp = NULL;
		}
		if (all && m_txtDoc)
		{
			m_txtDoc->Release();
			m_txtDoc = NULL;
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("OLE:");
		ASSERT(!"exception caught DC::UnInit");
		vLog("ERROR exception caught DC::UnInit s(%p), d(%p), t(%p)", m_sel, m_disp, m_txtDoc);
		m_sel = NULL;
		m_disp = NULL;
		if (all)
			m_txtDoc = NULL;
	}
}

void DocClass::SelectAll()
{
	if (GetSelObj() && m_sel)
		m_sel->SelectAll();
}

bool DocClass::GoTo(long ln, long col /* = 0*/, int extend /* = 0 */)
{
	DEFTIMERNOTE(DocGoToTimer, NULL);
	LOG((WTString("GoTo :") + itos(ln) + WTString(", ") + itos(col)).c_str());
	if (GetSelObj() && m_sel)
	{
		VARIANT extendVariant;
		extendVariant.vt = VT_I2;
		extendVariant.iVal = (short)extend;
		if (ln < 1)
			ln = 1;
		if (col < 1)
			col = 1;
		Trap2("Pos", FAILED(m_sel->MoveTo(ln, col, extendVariant)));

		long l, c;
		CurPos(l, c); // check to see where it really put the caret
		while (col > 0 && c > col)
		{
			// devstudio has a problem setting pos w/in mb comments
			VARIANT count;
			count.vt = VT_I2;
			count.iVal = 1;
			m_sel->CharLeft(extendVariant, count);
			CurPos(l, c);
		}

		if (ln != l && col == 1)
		{
			// requested to select to EOF but there was no EOL at EOF
			// go to end of last line
			Trap2("EOF", FAILED(m_sel->EndOfDocument(extendVariant)));
		}

		if (!extend)
			m_sel->Cancel();
	}
	return true;
}

bool DocClass::CurPos(long& line, long& col)
{
	DEFTIMERNOTE(DocCurPosTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		Trap2("Pos", FAILED(m_sel->get_CurrentLine(&line)));
		Trap2("Pos", FAILED(m_sel->get_CurrentColumn(&col)));
		m_line = line;
		m_col = col;
	}
	else
	{
		line = m_line;
		col = m_col;
	}
	return true;
}

long DocClass::GetTopLine()
{
	DEFTIMERNOTE(DocGetTopLineTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		long line;
		Trap2("Pos", FAILED(m_sel->get_TopLine(&line)));
		return line;
	}
	return 0;
}

bool DocClass::SelLine()
{
	DEFTIMERNOTE(DocSelLineTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		Trap2("SelLine", FAILED(m_sel->SelectLine()));
	}
	return true;
}

bool DocClass::SelWord(bool right /* = true */)
{
	ASSERT(FALSE);
	DEFTIMERNOTE(DocSelWordTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		VARIANT ext;
		ext.vt = VT_I2;
		ext.iVal = 1;
		VARIANT count;
		count.vt = VT_I2;
		count.iVal = 1;
		if (right)
			m_sel->WordRight(ext, count);
		else
			m_sel->WordLeft(ext, count);
	}
	return true;
}

CStringW DocClass::File()
{
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::F gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(!"unsafe thread access DC::F");
		return CStringW();
	}

	WTComBSTR bstr;
	if (m_txtDoc)
		Trap3(FAILED(m_txtDoc->get_FullName(&bstr)));
	CStringW file(bstr);
	file.MakeLower();
	return file;
}

short DocClass::ReadOnly()
{
	ASSERT(FALSE);
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::RO gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(FALSE);
		return false;
	}
	if (m_txtDoc)
	{
		short saved;
		m_txtDoc->get_ReadOnly(&saved);
		return saved;
	}
	return false;
}
short DocClass::SetReadOnly(int flg)
{
	ASSERT(FALSE);
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::RO gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(FALSE);
		return false;
	}
	if (m_txtDoc)
	{
		short saved = (short)(flg ? -1 : 0);
		m_txtDoc->put_ReadOnly(saved);
		return ReadOnly();
	}
	return false;
}

short DocClass::Saved()
{
	ASSERT(FALSE);
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::Sd gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(FALSE);
		return false;
	}
	if (m_txtDoc)
	{
		short saved;
		m_txtDoc->get_Saved(&saved);
		return saved;
	}
	return false;
}

WTString DocClass::GetCurSelection()
{
	DEFTIMERNOTE(DocGetCurSelectionTimer, NULL);
	WTComBSTR bstr;
	if (GetSelObj() && m_sel)
	{
		Trap3(FAILED(m_sel->get_Text(&bstr)));
	}
	return CStringW(bstr);
}

bool DocClass::HasColumnSelection()
{
	if (GetSelObj() && m_sel)
	{
		// is there any way to tell if the selection is regular or columnar?
	}
	return false;
}

bool DocClass::SetTextLines(LPCSTR text, int begLine, int nLines)
{
	ASSERT(FALSE);
	DEFTIMERNOTE(DocSetTextTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		GoTo(begLine + 1, 1);
		GoTo(begLine + nLines + 1, 2050, true); // rest of line
		VARIANT count;
		count.vt = VT_I2;
		count.iVal = 1;
		m_sel->Delete(count); // Brief doesn't do a destructive insert
		///////////////////////////////////////////////////////////////////
		m_sel->put_Text(WTComBSTR(WTString(text).Wide()));
	}
	return true;
}

bool DocClass::ReplaceSel(LPCSTR text)
{
	DEFTIMERNOTE(DocSetTextTimer, NULL);

	if (GetSelObj() && m_sel)
	{
		VARIANT count;
		count.vt = VT_I2;
		count.iVal = 1;
		WTString selstr = GetCurSelection();
		if (m_emulation != dsDevStudio && selstr.GetLength())
		{
			// some text is selected and needs deleting
			// brief and epsilon don't do destructive inserts
			if (m_emulation == dsEpsilon)
			{
				// bs and del don't nuke selection in epsilon mode
				// Cut is the easiest way to nuke it
				// this works in all emulations except brief
				// NOTE: "Epsilon (custom)" not supported
				WTString cpyBuf;
				if (OpenClipboard(MainWndH))
				{
					HANDLE hglb = GetClipboardData(CF_TEXT);
					if (hglb)
					{
						cpyBuf = (LPCTSTR)GlobalLock(hglb);
						GlobalUnlock(hglb);
					}
					CloseClipboard();
					// nuke text
					m_sel->Cut();
					// restore copy buffer
					if (OpenClipboard(MainWndH))
					{
						HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (uint)cpyBuf.GetLength() + 1);
						LPVOID pTxt = GlobalLock(hData);
						memcpy(pTxt, cpyBuf.c_str(), (uint)cpyBuf.GetLength() + 1);

						EmptyClipboard();
						SetClipboardData(CF_TEXT, hData);
						CloseClipboard();
						GlobalUnlock(hData);
					}
				}
			}
			else
			{
				// works in all emulations except epsilon
				m_sel->Delete(count);
			}
		}

		if (FAILED(m_sel->put_Text(WTComBSTR(WTString(text).Wide()))))
		{
			// unfortunately put_text does not return an error if the edit buffer is read-only
			Log(WTString("OleObj Failed: trap3").c_str());
			_ASSERTE(!"DocClass::ReplaceSel put_text failed");
			return false;
		}

		// unfortunately put_text does not return an error if the edit buffer is read-only
		return true;
	}
	else
	{
		return false;
	}
}

CStringW DocClass::GetFileName()
{
	WTComBSTR bname;
	m_txtDoc->get_FullName(&bname);
	return CStringW(bname);
}

bool DocClass::SetText(WTString text)
{
	ASSERT(FALSE);
	DEFTIMERNOTE(DocSetTextTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		WTString rstr = GetText();
		LPCSTR p1 = text.c_str(), p2 = rstr.c_str();
		int b = 0, e1 = text.GetLength(), e2 = rstr.GetLength();
		int lstart = 0;
		int pstart = 0;
		// find first difference
		for (; p1[b] && p1[b] == p2[b]; b++)
			if (p1[b] == '\n')
			{
				lstart++;
				pstart = b + 1;
			}

		if (p1[b] == p2[b])
		{ // no difference
			// insert and remove space so they know the file is modified
			GoTo(1, 1);
			m_sel->put_Text(WTComBSTR(L" "));
			VARIANT ivar;
			ivar.vt = VT_INT;
			ivar.intVal = 1;
			m_sel->Backspace(ivar);
			ASSERT(text == GetText());

			return true;
		}
		// find last difference
		for (; b < std::min(e1, e2) && p1[e1 - 1] == p2[e2 - 1]; e1--, e2--)
			;
		if (p1[e1] == '\n' && p2[e2] == '\n')
		{
			// don't split CR/LF
			e1++;
			e2++;
		}
		// find number of changed lines
		int lstop = lstart;
		// find lines modified
		for (int x = b; x < e2; x++)
		{
			if (p2[x] == '\n')
				lstop++;
		}
		// get rest of line up to CR/LF
		for (; p1[e1] && p1[e1] != '\n' && p1[e1] != '\r'; e1++)
			;
		// wrap lines greater than 2000 lines...
		int lnlen = 0;
#define VA_MAX_LINE_LEN 2010 // Includes safty margin for tab expansion, MS is 2048,
		for (int i = lstart; i < e1; i++, lnlen++)
		{
			if (p1[i] == '\n')
				lnlen = 0;
			else if (lnlen > VA_MAX_LINE_LEN)
			{
				text.insert(i - 1, "\r\n");
				SetText(text);
				return false;
			}
		}
		/* leave CRLF to preserve breakpoints
		if(p1[e1] == '\n')
		    e1++;
		*/

		GoTo(lstart + 1, 1);
		VARIANT count;
		count.vt = VT_I2;
		count.iVal = 1;

		while (lstop > (lstart + 1000))
		{
			// They will issue a message about the undo buffer if we replace
			// more than 3000 ish lines.
			// So we loop threw deleting 1000 lines at a time
			lstop -= 1000;
			GoTo(lstart + 1000 + 1, 1, true);
			m_sel->Delete(count); // Brief doesn't do a destructive insert
		}

		WTString s = text.substr(pstart, e1 - pstart);
		if (!p1[e1])                         // eof, select rest of last line
			GoTo(lstop + 1 + 1, 2050, true); // 2050 == max line length
		else
			GoTo(lstop + 1, 2050, true); // rest of line
#ifdef _DEBUG
		WTString cursel = GetCurSelection();
#endif // _DEBUG
       ///////////////////////////////////////////////////////////////////
       // We need to delete Selection before we insert text.
       // However we cant call delete if we don't have a selection
       // or we will delete the new line we are on.
       // If virtual space is enabled GetCurSelection() may return "",
       // however there is a selection that must be deleted or
       // it will insert the text we inserted and fill the rest of the
       // selections with tabs. ( i don't know why).
       // So, if we are not in column 1, we must have virtual space enabled and
       // there must be a selection that needs deleting.
		long ln, c;
		SetReadOnly(0);
		CurPos(ln, c);
		if ((lstart + 1) != ln || c > 1)
			m_sel->Delete(count); // Brief doesn't do a destructive insert
		///////////////////////////////////////////////////////////////////
		m_sel->put_Text(WTComBSTR(s.Wide()));

#ifdef _DEBUG
		int rdstate = ReadOnly();
		SetReadOnly(0);
		rdstate = ReadOnly();
		WTString theirtxt = GetText();
		if (text != theirtxt)
		{
			LPCSTR p1_2 = text.c_str();
			LPCSTR p2_2 = theirtxt.c_str();
			for (; *p1_2 == *p2_2; p1_2++, p2_2++)
				;
			ASSERT(FALSE);
		}
#endif
	}
	return true;
}

WTString DocClass::GetText()
{
	DEFTIMERNOTE(DocGetTextTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		long r, c;
		CurPos(r, c);
		GoTo(r, c); // in case there is a brief selection
		Trap3(FAILED(m_sel->SelectAll()));
		WTString str = GetCurSelection();
		GoTo(r, c);
		return str;
	}
	else
	{
		ASSERT(FALSE);
	}
	return WTString("");
}

void DocClass::WordPos(bool wback, int count, bool extend)
{
	ASSERT(FALSE);
	if (GetSelObj() && m_sel)
	{
		VARIANT ext;
		ext.vt = VT_I2;
		ext.iVal = (short)extend;
		VARIANT v;
		v.vt = VT_I2;
		v.iVal = (short)count;
		if (wback)
			m_sel->WordLeft(ext, v);
		else
			m_sel->WordRight(ext, v);
	}
}

bool DocClass::Undo()
{
	DEFTIMERNOTE(UndoAllTimer, NULL);
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::UA gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(!"unsafe thread access DC::UA");
		return false;
	}
	if (m_txtDoc)
	{
		short undid;
		Trap3(FAILED(m_txtDoc->Undo(&undid)));
		if (!undid)
			return false;
	}
	return true;
}

bool DocClass::UndoAll()
{
	ASSERT(FALSE);
	DEFTIMERNOTE(UndoAllTimer, NULL);
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::UA gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(FALSE);
		return false;
	}
	if (m_txtDoc)
	{
		while (Saved() == 0)
		{
			short undid;
			m_txtDoc->Undo(&undid);
			if (!undid)
				return false;
		}
	}
	return true;
}

WTString DocClass::FormatSel()
{
	DEFTIMERNOTE(DocFormatSelTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		Trap3(FAILED(m_sel->SmartFormat()));
		Trap3(FAILED(m_sel->SelectLine()));
		return GetCurSelection();
	}
	return WTString("");
}

int DocClass::GetTabSize()
{
	if (g_mainThread != GetCurrentThreadId())
	{
		vLog("ERROR unsafe thread access DC::GTS gmt(%08lx) cTID(%08lx)", g_mainThread, GetCurrentThreadId());
		ASSERT(!"unsafe thread access DC::GTS");
		return (int)Psettings->TabSize;
	}
	long sz = 0;
	if (m_txtDoc)
	{
		Trap3(FAILED(m_txtDoc->get_TabSize(&sz)));
	}
	else
		ASSERT(false);
	return (int)sz;
};

void DocClass::Indent()
{
	DEFTIMERNOTE(DocIndentTimer, NULL);
	if (GetSelObj() && m_sel)
	{
		VARIANT count;
		count.vt = VT_I2;
		count.iVal = 1;
		Trap3(FAILED(m_sel->Indent(count)));
	}
}

bool DocClass::GetTestEditorInfo()
{
	ASSERT(g_mainThread == GetCurrentThreadId());
	IDispatch* disp;
	short overtype; //, viewwhitespace;
	Trap2("GetTextEditorDisp", FAILED(g_pApp->get_TextEditor(&disp)));
	if (disp)
	{
		ITextEditor* ed;
		Trap2("GetTextEditor", FAILED(disp->QueryInterface(IID_ITextEditor, (void**)&ed)));
		if (ed)
		{
			ed->get_Overtype(&overtype);
			Psettings->m_overtype = (overtype != 0);
			//	ed->get_VisibleWhitespace(&viewwhitespace);
			ed->get_Emulation(&m_emulation);
			//	Psettings->m_bViewWhitespace = (viewwhitespace != 0);
			ed->Release();
			Psettings->TabSize = (DWORD)GetTabSize();
		}
		disp->Release();
	}
	return true;
}

bool DocClass::FindBookmark()
{
	if (GetSelObj() && m_sel)
	{
		VARIANT_BOOL rval;
		long ln, col;
		CurPos(ln, col);
		Trap3(FAILED(m_sel->SelectAll()));
		m_sel->NextBookmark(&rval);
		if (rval == -1)
			return true; // found a bookmark and moved curpos
		GoTo(ln, col);   // no bookmark found - restore old pos
		return false;
	}
	return false;
}

void DocClass::SetBookMark(bool set /* = true */)
{
	if (GetSelObj() && m_sel)
	{
		VARIANT_BOOL rval;
		if (set)
			m_sel->SetBookmark();
		else
			m_sel->ClearBookmark(&rval);
	}
}
