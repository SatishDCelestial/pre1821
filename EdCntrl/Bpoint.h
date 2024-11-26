#ifndef INCBPOINT_H
#define INCBPOINT_H

#include "OWLDefs.h"
class EdCnt;

typedef struct tagLINEATTR
{
	bool BrkPt;
	bool BkMrk;
	bool ModFlag;
	bool CurrentLine;
	bool CurrentError;
	DWORD BrkPtEn;   // 1 enabled, 2 disabled, -1 deleted
	UINT m_origLine; // the original line that came from MSDev
} LINEATTR;

class LineAttr : public LINEATTR
{
  public:
	LineAttr()
	{
		BrkPt = 0;
		Invalidate();
	}
	void SetAttr(LINEATTR* attr)
	{
		BrkPt = attr->BrkPt;
		BrkPtEn = attr->BrkPtEn;
		BkMrk = attr->BkMrk;
		CurrentLine = attr->CurrentLine;
		CurrentError = attr->CurrentError;
		m_origLine = attr->m_origLine;
	}
	void ToggleBreakPointEn()
	{
		if (BrkPt)
		{
			if (BrkPtEn == 1)
				BrkPtEn = false;
			else
				BrkPtEn = true;
		}
	}
	void SetModify()
	{
		ModFlag = true;
	}
	void SetError()
	{
		CurrentError = true;
	}
	bool ToggleMod()
	{
		if (ModFlag)
		{
			ModFlag = false;
		}
		else
		{
			ModFlag = true;
		}
		return ModFlag;
	}
	bool HasBookMark(bool findBookmark)
	{
		if (findBookmark)
			return BkMrk;
		else
			return ModFlag;
	}
	// if line has brkpt, then LOWORD() of the return will have state (enabled/disabled)
	DWORD HasBrkpt()
	{
		if (BrkPt && BrkPtEn != -1)
			return 0x10000000 | BrkPtEn;
		return false;
	}
	bool ToggleBreakPoint(int line = -1)
	{
		if (BrkPt && BrkPtEn == 1)
		{
			BrkPt = BrkPtEn = false;
			m_origLine = 0;
		}
		else if (BrkPt && !BrkPtEn)
		{
			BrkPtEn = true;
		}
		else
		{
			BrkPt = BrkPtEn = true;
			m_origLine = (UINT)line;
		}
		return BrkPt;
	}
	bool ToggleBookmark()
	{
		if (BkMrk)
		{
			BkMrk = false;
		}
		else
		{
			BkMrk = true;
		}
		return BkMrk;
	}
	void Invalidate()
	{
		if (BrkPt)
		{
			// if there was a brkpt, mark it for deletion
			// When we sync w/ msdev we'll invalidate the rest the brkpt stuff
			BrkPtEn = (DWORD)-1;
		}
		else
			m_origLine = BrkPtEn = BrkPt = 0;
		BkMrk = ModFlag = CurrentLine = CurrentError = false;
	}
	bool IsValid()
	{
		return (BrkPt || BrkPtEn || BkMrk || CurrentLine || CurrentError || ModFlag);
	}
	void AddBookMark(bool add = true)
	{
		BkMrk = add;
	}
	void AddBreakPoint(int line, bool add = true)
	{
		BrkPt = add;
		m_origLine = (UINT)line;
		EnableBreakPoint(add);
	}
	void EnableBreakPoint(bool enable = true)
	{
		BrkPtEn = enable;
	}
};

class AttrLst
{
  public:
	AttrLst* m_next;
	int m_ln;
	LineAttr m_data;

	AttrLst(int ln = 0, AttrLst* next = NULL)
	{
		m_next = next;
		m_ln = ln;
	}
	AttrLst* Find(int ln)
	{
		AttrLst* pLst = this;
		while (pLst && pLst->m_ln < ln)
			pLst = pLst->m_next;
		return pLst;
	}
	LineAttr* Line(int ln)
	{
		AttrLst* pLst = this;
		ASSERT(pLst->m_ln == 0);
		ASSERT(ln < 50000 && ln > -1);
		while (pLst->m_next && pLst->m_next->m_ln < ln)
			pLst = pLst->m_next;

		if (pLst->m_next && pLst->m_next->m_ln == ln) // line already  exists
			return &pLst->m_next->m_data;
		pLst->m_next = new AttrLst(ln, pLst->m_next);
		return &pLst->m_next->m_data;
	}
	void DeleteLines(EdCnt* ed, int l1, int l2)
	{
		AddBlankLine(ed, l1, l1 - l2);
	}
	void AddBlankLine(EdCnt* ed, int ln, int count = 1);
#pragma warning(disable : 4310)
	void DeleteNulls()
	{
		AttrLst* p = this;
		ASSERT(p->m_ln == 0);
		while (p && p->m_next)
		{
			// New objects are filled with 0xCD when they are allocated in debug builds
			// The freed blocks kept unused in the debug heap's linked list are filled with 0xDD in debug builds
			// debug build "NoMansLand" buffers on either side of the memory used by an app are filled with 0xFD
			ASSERT(p != (AttrLst*)(uintptr_t)0xCDCDCDCDCDCDCDCDull);
			if (p->m_next->m_data.IsValid() == false)
				p->DeleteNext();
			else
				p = p->m_next;
		}
	}
	void VerifyLines(int lineCnt, EdCnt* ed);
	void RemoveDeletedBrkpts()
	{
		// called after syncing w/ addin side & MSDev
		AttrLst* p = this;
		ASSERT(p->m_ln == 0);
		while (p)
		{
			ASSERT(p != (AttrLst*)(uintptr_t)0xCDCDCDCDCDCDCDCDull);
			if (p->m_data.BrkPtEn == -1)
			{
				p->m_data.BrkPt = 0;
				p->m_data.m_origLine = 0;
				p->m_data.BrkPtEn = 0;
			}
			p = p->m_next;
		}
		DeleteNulls();
	}
#pragma warning(default : 4310)
	void DeleteNext()
	{
		AttrLst* p = m_next;
		if (p)
		{
			m_next = p->m_next;
			p->m_next = NULL;
			delete p;
		}
	}
	~AttrLst()
	{
		// recursive delete caused stack overflow
		// changed so first deletsed iterates threw list
		// there is probably a better way?
		if (m_next)
		{
			// top of list, delete all next
			while (m_next)
			{
				// iterate threw list freeing each
				AttrLst* nnxt = m_next->m_next;
				// dont let them delete next, we will
				m_next->m_next = NULL;
				delete m_next;
				m_next = nnxt;
			}
			delete m_next;
			m_next = NULL;
		}
	}
	void Format(EdCnt* ed, int l1 = 0, int l2 = NPOS, BOOL useFlash = FALSE, COLORREF crFlash = 0x000000);
	int NextBreakpointFromLine(int* ln, int* enabled)
	{
		AttrLst* p = this;
		while (p)
		{ // find next line
			if (p->m_ln > *ln && p->m_data.BrkPt)
			{
				ASSERT(p->m_ln < 50000 && p->m_ln > -1);
				*ln = p->m_ln;
				*enabled = (int)p->m_data.BrkPtEn;
				return (int)p->m_data.m_origLine;
			}
			p = p->m_next;
		}
		return 0;
	}
	bool NextBookmarkFromLine(int& ln, bool bookmark = true)
	{
		bool firstPass = true;
		AttrLst* p = this;
		while (p)
		{ // find next line
			if ((p->m_ln > ln || !firstPass) && p->m_data.HasBookMark(bookmark))
			{
				ln = p->m_ln;
				ASSERT(ln < 50000 && ln > -1);
				return true;
			}
			p = p->m_next;
			if (!p && firstPass)
			{
				p = this;
				firstPass = false;
			}
		}
		// no bookmarks - try for editmarks
		if (bookmark)
			return NextBookmarkFromLine(ln, false);
		return false;
	}
	bool PrevBookmarkFromLine(int& ln, bool bookmark = true);
	void ClearAllBrkPts(EdCnt* ed);
	void ClearAllBookmarks(EdCnt* ed);
	void ClearCurrentLine(EdCnt* ed);
	void ClearCurrentError(EdCnt* ed);
	void ClearAllEditmarks(EdCnt* ed);
};
#endif
