#ifndef INCOLEOBJH
#define INCOLEOBJH

#include "OWLDefs.h"
#include "WTString.h"

interface IApplication;
interface ITextSelection;
interface ITextDocument;
interface IDispatch;

class DocClass
{
	ITextDocument* m_txtDoc;
	ITextSelection* m_sel;
	IDispatch* m_disp;
	long m_line, m_col;
	long m_indent;
	long m_emulation;

  public:
	long m_begSel;
	void SelectAll();
	DocClass(ITextDocument* pDoc);
	CStringW GetFileName();
	~DocClass();
	bool GetSelObj();
	void UnInit(bool all = false);
	CStringW File();
	bool SelWord(bool right = true);
	long GetTopLine(); // gets first visible line
	bool SelLine();
	short ReadOnly();
	short SetReadOnly(int flg);
	bool GoTo(long ln, long col = 0, int extend = 0);
	bool CurPos(long& line, long& col);
	short Saved();
	WTString GetCurSelection();
	bool SetText(WTString text);
	bool SetTextLines(LPCSTR text, int begLine, int nLines);
	WTString GetText();
	bool UndoAll();
	bool Undo();
	WTString FormatSel();
	int GetTabSize();
	void Indent();
	bool GetTestEditorInfo();
	bool ReplaceSel(LPCSTR text);
	void WordPos(bool left, int count, bool extend);
	bool FindBookmark();
	void SetBookMark(bool set = true);
	long Emulation()
	{
		return m_emulation;
	};
	bool HasColumnSelection();
};

const char* Hex(int i);
extern IApplication* g_pApp;

#endif
