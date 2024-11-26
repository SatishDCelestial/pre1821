#pragma once

extern int g_screenXOffset;
int GetColumnOffset(LPCSTR line, int pos);
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

class EdCntCWnd : public EdCnt
{
  public:
	EdCntCWnd()
	{
	}

#pragma region IEdCnt
	virtual bool IsInitialized() const
	{
		return true;
	}

	virtual CPoint vGetCaretPos() const
	{
		return CWnd::GetCaretPos();
	}

	virtual void vGetClientRect(LPRECT lpRect) const
	{
		return CWnd::GetClientRect(lpRect);
	}

	virtual void vClientToScreen(LPPOINT lpPoint) const
	{
		CWnd::ClientToScreen(lpPoint);
	}

	virtual void vClientToScreen(LPRECT lpRect) const
	{
		CWnd::ClientToScreen(lpRect);
	}

	virtual void vScreenToClient(LPPOINT lpPoint) const
	{
		CWnd::ScreenToClient(lpPoint);
	}

	virtual void vScreenToClient(LPRECT lpRect) const
	{
		CWnd::ScreenToClient(lpRect);
	}

	virtual CWnd* vGetFocus() const
	{
		return CWnd::GetFocus();
	}

	virtual CWnd* vSetFocus()
	{
		CWnd* foc = CWnd::SetFocus();
		CWnd::SetActiveWindow();
		return foc;
	}

	virtual CPoint GetCharPos(long lChar)
	{
		CPoint pt, rpt;
		const WTString bb(GetBufConst());
		PosToLC(bb, lChar, pt.y, pt.x /*, TRUE*/);
		LPCSTR p = bb.c_str() + GetBufIndex(bb, TERRCTOLONG(pt.y, 1));
		rpt.x = GetColumnOffset(p, pt.x - 1) + g_screenXOffset;
		rpt.y = (pt.y - m_firstVisibleLine) * g_FontSettings->GetCharHeight();
		if (gShellAttr->IsDevenv() || pt.x == 0) // caret at col 1 is skewed.
			rpt.x++;
		return rpt;
	}

	virtual int CharFromPos(POINT* pt, bool resolveVc6Tabs)
	{
#ifdef TI_BUILD
		SendMessage(CC_SCREEN_TO_CHAR_OFFSET, (WPARAM)pt, (WPARAM)&cpt);
		return TERRCTOLONG(cpt.y, cpt.x);
#else
		// TODO find logic to add number of chars offset to pt
		long l, c;
		GetPos(l, c);

		CPoint cpt(vGetCaretPos());
		bool caretIsScrolledLeft = false;
		if (g_screenXOffset < 0 && cpt.x < 0)
			caretIsScrolledLeft = true;

		if (cpt.x && cpt.x == (LONG)Psettings->m_borderWidth)
			cpt.x++;
		if (!caretIsScrolledLeft && cpt.x < g_FontSettings->GetCharWidth())
			cpt.x = 2;
		if (pt->y < cpt.y)
			l--;
		if (g_FontSettings->CharSizeOk())
		{
			l = max(l + (pt->y - cpt.y) / g_FontSettings->GetCharHeight(), 0);
			if (caretIsScrolledLeft)
				c = max(c + ((pt->x - cpt.x - g_screenXOffset) / g_FontSettings->GetCharWidth()), 0);
			else
				c = max(c + ((pt->x - cpt.x) / g_FontSettings->GetCharWidth()), 0);
		}

#ifndef RAD_STUDIO
		if (resolveVc6Tabs || gShellAttr->IsDevenv())
		{
			// translate screenpos to char offset
			// do reverse tab/col calc that GetCollumnOffset does in ter_mfc.cpp
			const WTString bb(GetBufConst());
			int o = GetBufIndex(bb, TERRCTOLONG(l, 1));
			int len = bb.GetLength();
			LPCSTR p = bb.c_str();
			int col = 0;
			uint tabSz = Psettings->TabSize;

			int i;
			for (i = 0; col < c && (o + i) < len; i++)
			{
				col++;
				if (p[o + i] == '\t' && Psettings->TabSize)
					col = int((col + tabSz - 1) / Psettings->TabSize * Psettings->TabSize);
			}
			c = i;
		}
#endif // RAD_STUDIO
		return TERRCTOLONG(l, c);
#endif // TI_BUILD
	}

	virtual long GetFirstVisibleLine()
	{
		if (::GetFocus() == GetSafeHwnd())
		{
			long l, c;
			GetPos(l, c);

			CPoint cpt(vGetCaretPos());
			if (g_FontSettings->CharSizeOk())
				l = max(l - (cpt.y) / g_FontSettings->GetCharHeight(), 0);
			m_firstVisibleLine = l;
			return l;
		}
		return m_firstVisibleLine;
	}

	virtual void GetVisibleLineRange(long& first, long& last)
	{
		first = GetFirstVisibleLine();
		CRect rc;
		vGetClientRect(&rc);
		last = first + rc.Height() / g_FontSettings->GetCharHeight();
	}

	virtual bool TryToShow(long lStartPt, long lEndPt, HowToShow howToShow = HowToShow::ShowAsIs)
	{
		_ASSERTE(!"Not implemented!");
		return false;
	}

	virtual bool HasBlockOrMultiSelection() const
	{
		return gShellSvc && gShellSvc->HasBlockModeSelection(this);
	}
#pragma endregion IEdCnt

  private:
	virtual void GetPos(long& l, long& c)
	{
		_ASSERTE(gShellAttr->IsMsdev() || gShellAttr->IsCppBuilder());
		l = c = 0;
		if (m_pDoc)
			m_pDoc->CurPos(l, c);
		else
		{
			uint p = CurPos();
			l = (long)TERROW(p);
			c = (long)TERCOL(p);
		}
	}
};
