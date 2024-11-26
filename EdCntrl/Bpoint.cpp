#include "stdafxed.h"
#include "bpoint.h"
#include "edcnt.h"
#include "settings.h"
#include "bordercwnd.h"
#include "VATree.h"
#include "fontSettings.h"
#include "timer.h"
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void AttrLst::Format(EdCnt* ed, int l1, int l2 /*= NPOS*/, BOOL useFlash /*= FALSE*/, COLORREF crFlash /*= 0x000000*/)
{
	ASSERT(0);
	/*
	        if(!ed || !ed->GetBorder())
	            return;

	        DEFTIMERNOTE(AttrLst_Format, NULL);
	        CDC *pdc = ed->GetBorder()->GetDC();
	        CRect brect, edrect;
	        ed->GetBorder()->GetClientRect(&brect);
	        ed->GetClientRect(&edrect);
	        CRect tmpEdRc(edrect);

	        CDC memdc;
	        CBitmap bmp;
	        // create memdc & create storage bitmap
	        memdc.CreateCompatibleDC(pdc);
	        bmp.CreateCompatibleBitmap(pdc, brect.Width(), brect.Height());
	        CBitmap *pOldMemBmp = memdc.SelectObject(&bmp);

	        tmpEdRc.right = BORDER_WIDTH+FRAME_WIDTH;
	*/
	/*
	        memdc.FillSolidRect(tmpEdRc, useFlash ? crFlash : Psettings->m_colors[C_SelectionMargin].c_bg);
	        if (!useFlash)
	        {
	            tmpEdRc.left = --tmpEdRc.right - 2;
	            memdc.FillSolidRect(tmpEdRc, Psettings->m_colors[C_SelectionMargin].c_fg);
	        }

	        // get first and last visible line
	        l1 = ed->GetFirstVisibleLine();
	        l2 = ed->GetLastVisibleLine()+1;

	        // limit to client rect
	        CRgn rgnRect;
	        rgnRect.CreateRectRgnIndirect(&edrect);
	        pdc->SelectClipRgn(&rgnRect);

	        int w = brect.Width()-1;	// don't paint on the right edge
	        int h = g_FontSettings->m_TxtFontHeight;

	        // delete all unset lines...
	        DeleteNulls(); // delete all nulls;
	        AttrLst *p = this;
	        // found a case where bookmarks weren't being displayed because p->m_ln was garbage
	        ASSERT(p->m_ln < 50000 && p->m_ln > -1);
	        while(p && p->m_ln < l1)
	            p = p->m_next;
	        while(p && p->m_ln <= l2){
	            if (p->m_data.BrkPt || p->m_data.BkMrk || p->m_data.CurrentLine ||
	                p->m_data.CurrentError ||
	                (p->m_data.ModFlag && Psettings->m_showEditMarks))
	            {
	                uint cpos = ed->LinePos(p->m_ln);
	                if(cpos != NPOS){
	                    CPoint pt = ed->GetCharPos(cpos);
	                    CRect bkRct(0, pt.y, w, pt.y+h);
	                    CBrush* oldBrush;

	                    if(p->m_data.BkMrk){
	                        CPoint rndPt(5, 5);
	                        CBrush bookmark_color(Psettings->m_colors[C_Bookmark].c_bg);
	                        oldBrush = memdc.SelectObject(&bookmark_color);
	                        CPen pen(PS_SOLID, 1, Psettings->m_colors[C_Bookmark].c_fg);
	                        CPen* oldPen = (CPen*)memdc.SelectObject(&pen);
	                        memdc.RoundRect(bkRct, rndPt);
	                        memdc.SelectObject(oldPen);
	                        memdc.SelectObject(oldBrush);
	                    }
	                    if(p->m_data.CurrentError)
	                    {
	                        CRect tickRct(0, pt.y, w+1, pt.y+w+1);
	                        tickRct.DeflateRect(1, 1);
	                        tickRct.OffsetRect(4,-3);

	                        int hunit = tickRct.Width()/3;
	                        int vunit = tickRct.Height()/3;
	                        POINT aPts[5] = {
	                            {0*hunit+ tickRct.left, tickRct.top+1*vunit},
	                            {(int)(1.5*hunit+ tickRct.left), tickRct.top+1*vunit},
	                            {2*hunit+ tickRct.left, (int)(tickRct.top+1.5*vunit)},
	                            {(int)(1.5*hunit+ tickRct.left), tickRct.top+2*vunit},
	                            {0*hunit+ tickRct.left, tickRct.top+2*vunit}
	                        };
	                        CBrush curline_color(Psettings->m_colors[C_CurrentError].c_bg);
	                        oldBrush = memdc.SelectObject(&curline_color);
	                        CPen pen(PS_SOLID, 1, Psettings->m_colors[C_CurrentError].c_fg);
	                        CPen* oldPen = (CPen*)memdc.SelectObject(&pen);
	                        memdc.Polygon(aPts, 5);
	                        memdc.SelectObject(oldPen);
	                        memdc.SelectObject(oldBrush);
	                    }
	                    if(p->m_data.BrkPt){
	                        CRect bpRct(bkRct);
	                        bpRct.DeflateRect(1, 1);
	                        bpRct.right = bpRct.left + bpRct.Height();
	                        bpRct.OffsetRect(3, 0);
	                        CPoint rndPt(h, h);
	                        if(p->m_data.BrkPtEn == 1){
	                            CBrush bkpt_color(Psettings->m_colors[C_BrkPt].c_bg);
	                            oldBrush = memdc.SelectObject(&bkpt_color);
	                            CPen pen(PS_SOLID, 1, Psettings->m_colors[C_BrkPt].c_bg);
	                            CPen* oldPen = (CPen*)memdc.SelectObject(&pen);
	                            memdc.RoundRect(bpRct, rndPt);
	                            memdc.SelectObject(oldPen);
	                            memdc.SelectObject(oldBrush);
	                        } else if (!p->m_data.BrkPtEn) {
	                            CBrush white(Psettings->m_colors[C_BrkPt].c_fg);
	                            oldBrush = memdc.SelectObject(&white);
	                            memdc.RoundRect(bpRct, rndPt);
	                            memdc.SelectObject(oldBrush);
	                        }
	                    }
	                    if(p->m_data.ModFlag && Psettings->m_showEditMarks){
	                        CPoint rndPt(2, 2);
	                        CPoint ctr = bkRct.CenterPoint();
	                        CRect modRct(ctr, ctr);
	                        modRct.InflateRect(2, 2);
	                        modRct.OffsetRect(1, 0);
	                        CBrush ModColor(Psettings->m_colors[C_Bookmark].c_bg);
	                        oldBrush = memdc.SelectObject(&ModColor);
	                        CPen pen(PS_SOLID, 1, Psettings->m_colors[C_Bookmark].c_fg);
	                        CPen* oldPen = (CPen*)memdc.SelectObject(&pen);
	                        memdc.RoundRect(modRct, rndPt);
	                        memdc.SelectObject(oldPen);
	                        memdc.SelectObject(oldBrush);
	                    }
	                    if(p->m_data.CurrentLine && 1 == g_dbgState){
	                        CRect arrowRct(0, pt.y, w+1, pt.y+w+1);
	                        arrowRct.DeflateRect(3, 3);
	                        // center arrow on line
	                        CPoint ctr = bkRct.CenterPoint();
	                        CPoint arrowCtr = arrowRct.CenterPoint();
	                        int diff = ctr.y - arrowCtr.y;
	                        if (diff)
	                            arrowRct.OffsetRect(0, diff);

	                        int hunit = arrowRct.Width()/3+1;
	                        int vunit = arrowRct.Height()/3;
	                        POINT aPts[7] = {
	                            {0*hunit+ arrowRct.left, arrowRct.top+1*vunit},
	                            {1*hunit+ arrowRct.left, arrowRct.top+1*vunit},
	                            {1*hunit+ arrowRct.left, arrowRct.top+0*vunit},
	                            {2*hunit+ arrowRct.left +1, (int)(arrowRct.top+1.5*vunit)},
	                            {1*hunit+ arrowRct.left, arrowRct.top+ 3*vunit},
	                            {1*hunit+ arrowRct.left, arrowRct.top+2*vunit},
	                            {0*hunit+ arrowRct.left, arrowRct.top+2*vunit}
	                        };
	                        CBrush curline_color(Psettings->m_colors[C_CurrentLine].c_bg);
	                        oldBrush = memdc.SelectObject(&curline_color);
	                        CPen pen(PS_SOLID, 1, Psettings->m_colors[C_CurrentLine].c_fg);
	                        CPen* oldPen = (CPen*)memdc.SelectObject(&pen);
	                        memdc.Polygon(aPts, 7);
	                        memdc.SelectObject(oldPen);
	                        memdc.SelectObject(oldBrush);
	                    }
	                }
	            }
	            p = p->m_next;
	        }
	*/
	/*
	        pdc->BitBlt(0, 0, brect.Width(), brect.Height(), &memdc, 0, 0, SRCCOPY);
	        ed->ValidateRect(CRect(0, 0, brect.Width(), brect.Height()));
	        memdc.SelectObject(pOldMemBmp);
	        memdc.DeleteDC();
	        ed->ReleaseDC(pdc);
	*/
}

void AttrLst::ClearAllBrkPts(EdCnt* ed)
{
	for (AttrLst* p = this; p; p = p->m_next)
	{ // find next line
		if (p->m_data.BrkPt)
			p->m_data.ToggleBreakPoint();
		// in case the above call only enabled a disabled brkpt
		if (p->m_data.BrkPt)
			p->m_data.ToggleBreakPoint();
	}
}

void AttrLst::ClearAllBookmarks(EdCnt* ed)
{
	for (AttrLst* p = this; p; p = p->m_next)
	{ // find next line
		if (p->m_data.BkMrk)
		{
			p->m_data.ToggleBookmark();
			if (g_VATabTree)
				g_VATabTree->ToggleBookmark(ed->FileName(), p->m_ln, false);
		}
	}
}

void AttrLst::ClearAllEditmarks(EdCnt* ed)
{
	for (AttrLst* p = this; p; p = p->m_next)
	{ // find next line
		if (p->m_data.ModFlag)
		{
			p->m_data.ToggleMod();
		}
	}
}

bool AttrLst::PrevBookmarkFromLine(int& ln, bool bookmk /* = true*/)
{
	bool firstPass = true;
	AttrLst* p = this;
	int currentWinner = -1;
	while (p)
	{ // go thru all lines
		if ((p->m_ln < ln) && p->m_data.HasBookMark(bookmk) && (p->m_ln > currentWinner))
		{
			currentWinner = p->m_ln;
		}
		p = p->m_next;
		if (!p && firstPass)
		{
			p = this;
			firstPass = false;
		}
	}

	if (currentWinner == -1)
	{
		for (p = this; p; p = p->m_next)
		{ // go thru all lines and get the last bookmark
			if ((p->m_ln >= ln) && p->m_data.HasBookMark(bookmk))
			{
				currentWinner = p->m_ln;
			}
		}
		if (currentWinner == -1)
		{
			// no bookmarks - try for editmarks
			if (bookmk)
				return PrevBookmarkFromLine(ln, false);
			return false;
		}
	}
	ln = currentWinner;
	return true;
}

void AttrLst::ClearCurrentLine(EdCnt* ed)
{
	for (AttrLst* p = this; p; p = p->m_next)
	{ // find next line
		if (p->m_data.CurrentLine)
		{
			p->m_data.CurrentLine = false;
		}
	}
}

void AttrLst::ClearCurrentError(EdCnt* ed)
{
	for (AttrLst* p = this; p; p = p->m_next)
	{ // find next line
		if (p->m_data.CurrentError)
		{
			p->m_data.CurrentError = false;
		}
	}
}

void AttrLst::AddBlankLine(EdCnt* ed, int ln, int count)
{
	AttrLst* pLst = this;
	ASSERT(pLst->m_ln == 0);
	ASSERT(ln < 50000 && ln > -1);
	ASSERT(count < 50000 && count > -50000);
	DEFTIMERNOTE(AttrLst_AddBlankLine, NULL);
	if (g_VATabTree)
		g_VATabTree->RemoveAllBookmarks(ed->FileName());
	while (pLst && pLst->m_ln < ln)
	{
		if (pLst->m_data.BkMrk && g_VATabTree)
			g_VATabTree->ToggleBookmark(ed->FileName(), pLst->m_ln, true);
		pLst = pLst->m_next;
	}
	if (count < 0)
	{ // delete lines
		while (pLst && pLst->m_ln + count < ln)
		{
			pLst->m_data.Invalidate();
			pLst = pLst->m_next;
		}
	}
	// insert after first line since we use that for the top of list
	if (pLst == this)
		pLst = m_next;

	while (pLst)
	{
		ASSERT(pLst != this);
		pLst->m_ln += count;
		if (pLst->m_data.BkMrk && g_VATabTree)
			g_VATabTree->ToggleBookmark(ed->FileName(), pLst->m_ln, true);
		ASSERT(pLst->m_ln < 50000 && pLst->m_ln > -50000);
		pLst = pLst->m_next;
	}

	// make sure there aren't any duplicate entries
	pLst = this;
	ASSERT(pLst->m_ln == 0);
	while (pLst && pLst->m_next)
	{
		if (pLst->m_ln == pLst->m_next->m_ln)
		{
			pLst->m_ln = 0;
			if (pLst->m_data.BkMrk)
				pLst->m_next->m_data.BkMrk = true;
			if (pLst->m_data.BrkPt)
				pLst->m_next->m_data.BrkPt = true;
			if (pLst->m_data.CurrentError)
				pLst->m_next->m_data.CurrentError = true;
			if (pLst->m_data.CurrentLine)
				pLst->m_next->m_data.CurrentLine = true;
			if (pLst->m_data.ModFlag)
				pLst->m_next->m_data.ModFlag = true;
			if (pLst->m_data.m_origLine)
				pLst->m_next->m_data.m_origLine = pLst->m_data.m_origLine;
			if (pLst->m_data.BrkPtEn)
				pLst->m_next->m_data.BrkPtEn = pLst->m_data.BrkPtEn;
			if (g_VATabTree && pLst->m_data.BkMrk)
				g_VATabTree->ToggleBookmark(ed->FileName(), pLst->m_ln, false);
			pLst->m_data.Invalidate();
		}
		pLst = pLst->m_next;
	}
	DeleteNulls();
}

#pragma warning(disable : 4310)
void AttrLst::VerifyLines(int lineCnt, EdCnt* ed)
{
	AttrLst* p = this;
	ASSERT(p->m_ln == 0);
	while (p)
	{
		ASSERT(p != (AttrLst*)(uintptr_t)0xCDCDCDCDCDCDCDCDull);
		if (p->m_ln > lineCnt)
		{
			if (g_VATabTree && p->m_data.BkMrk)
				g_VATabTree->ToggleBookmark(ed->FileName(), p->m_ln, false);
			p->m_ln = 0;
			p->m_data.Invalidate();
		}
		p = p->m_next;
	}
}
