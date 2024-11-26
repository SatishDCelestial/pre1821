#pragma once

#include "EdCntCwnd.h"
#include "VaService.h"

class EdCntCWndVs : public EdCntCWnd
{
  public:
	EdCntCWndVs()
	{
	}

#pragma region IEdCnt

	// 	virtual long	GetFirstVisibleLine()
	// 	{
	// 		if (m_IVsTextView)
	// 		{
	// 			// firstVisLine will be incorrect if the caret is after a hidden
	// 			// region that has scrolled out of view.  firstVisLine will act
	// 			// as if region is expanded (first s/b 108 but is reported as 88)
	// 			long iMinUnit, iMaxUnit, iVisibleUnits, iFirstVisibleUnit;
	// 			m_IVsTextView->GetScrollInfo(SB_VERT, &iMinUnit, &iMaxUnit, &iVisibleUnits, &iFirstVisibleUnit);
	// 			m_firstVisibleLine = iFirstVisibleUnit + 1;
	// 		}
	// 		else
	// 		{
	// 			// firstVisLine will be incorrect if a hidden region is on the screen
	// 			// and the caret is after it (first s/b 88 but is reported as 108)
	// 			// ok if caret if before any visible hidden regions
	// 			EdCntCWnd::GetFirstVisibleLine();
	// 		}
	//
	// 		return m_firstVisibleLine;
	// 	}

	virtual void GetVisibleLineRange(long& first, long& last)
	{
		const long kMaxLines = 10000;

		CRect rc;
		vGetClientRect(&rc);
		const long originalFirst = EdCntCWnd::GetFirstVisibleLine();
		long secondTryFirst = originalFirst;
		if (m_IVsTextView)
		{
			long iMinUnit, iMaxUnit, iVisibleUnits, iFirstVisibleUnit = 0;
			m_IVsTextView->GetScrollInfo(SB_VERT, &iMinUnit, &iMaxUnit, &iVisibleUnits, &iFirstVisibleUnit);
			secondTryFirst = iFirstVisibleUnit + 1;
		}

		if (originalFirst >= secondTryFirst)
		{
			// first vis line can be wrong if there are hidden regions (see commented out GetFirstVisibleLine above)
			// Don't know whether originalFirst or secondTryFirst is correct;
			// just take the lesser of the two.
			// Make 'last' based on the larger of the two.
			first = secondTryFirst;
			last = originalFirst + rc.Height() / g_FontSettings->GetCharHeight();
		}
		else
		{
			// first vis line can be wrong if there are hidden regions (see commented out GetFirstVisibleLine above)
			// Don't know whether originalFirst or secondTryFirst is correct;
			// just take the lesser of the two.
			// Make 'last' based on the larger of the two.
			first = originalFirst;
			last = secondTryFirst + rc.Height() / g_FontSettings->GetCharHeight();
		}

		if (originalFirst > kMaxLines && secondTryFirst > kMaxLines)
			return; // too many lines? (note that vs2008 doesn't even produce regions for test_large_file.cpp)

		if (!m_IVsTextView)
			return;

		if (!gVsTextManager)
		{
			if (!gPkgServiceProvider)
				return;

			IUnknown* tmp = NULL;
			gPkgServiceProvider->QueryService(SID_SVsTextManager, IID_IVsTextManager, (void**)&tmp);
			gVsTextManager = tmp;
			if (!gVsTextManager)
				return;

			if (!gVsHiddenTextManager)
			{
				gVsHiddenTextManager = gVsTextManager;
				if (!gVsHiddenTextManager)
					return;
			}
		}

		CComPtr<IVsTextLines> buffer;
		m_IVsTextView->GetBuffer(&buffer);
		if (!buffer)
			return;

		// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.textmanager.interop.ivshiddentextsession%28VS.80%29.aspx
		// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.textmanager.interop.ivshiddentextclient%28VS.80%29.aspx
		CComPtr<IVsHiddenTextSession> hiddenSession;
		_ASSERTE(gVsHiddenTextManager);
		gVsHiddenTextManager->GetHiddenTextSession(buffer, &hiddenSession);
		if (!hiddenSession)
			return;

		// EnumHiddenRegions in IVsHiddenTextsession
		CComPtr<IVsEnumHiddenRegions> enumHiddenRegions;
		// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.textmanager.interop.find_hidden_region_flags%28v=VS.100%29.aspx
		// http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.textmanager.interop.ivshiddentextsession.enumhiddenregions%28v=VS.80%29.aspx
		TextSpan queryTextSpan = {-1, (first > 1 ? first - 1 : first), -1, kMaxLines};
		hiddenSession->EnumHiddenRegions(FHR_VISIBLE_ONLY | FHR_INTERSECTS_SPAN, 0, &queryTextSpan, &enumHiddenRegions);
		if (!enumHiddenRegions)
			return;

		ULONG regionCnt = 0, hitRegionCnt = 0;
		if (FAILED(enumHiddenRegions->GetCount(&regionCnt)) || !regionCnt)
			return;

		CComPtr<IVsHiddenRegion> curRegion;
		while (SUCCEEDED(enumHiddenRegions->Next(1, &curRegion, NULL)) && curRegion)
		{
			TextSpan regionTxtSpan;
			if (SUCCEEDED(curRegion->GetSpan(&regionTxtSpan)))
			{
				if (regionTxtSpan.iStartLine > last)
					break; // no need to iterate over regions that are after lastVisLine

				DWORD regionState = 0;
				if (SUCCEEDED(curRegion->GetState(&regionState)))
				{
					if (regionState == hrsDefault)
					{
						// region is hidden
						// adjust lastLine by number of lines in it (if it starts after firstVisLine)
						if (regionTxtSpan.iStartLine >= first)
							last += regionTxtSpan.iEndLine - regionTxtSpan.iStartLine;
					}
				}
			}
			curRegion.Release();
			++hitRegionCnt;
		}

#ifdef _DEBUGxx
		CStringW msg;
		CString__FormatW(msg, L"VisibleLineRange: first %d, last %d, hit %d of %d regions\n", first, last, hitRegionCnt,
		                 regionCnt);
		OutputDebugStringW(msg);
#endif // _DEBUG
	}

	virtual bool TryToShow(long lStartPt, long lEndPt, HowToShow howToShow = HowToShow::ShowAsIs)
	{
		_ASSERTE(m_IVsTextView != nullptr);

		if (m_IVsTextView == nullptr)
			return false;

		TextSpan span;
		if (SUCCEEDED(m_IVsTextView->GetLineAndColumn(min(lStartPt, lEndPt), &span.iStartLine, &span.iStartIndex)) &&
		    SUCCEEDED(m_IVsTextView->GetLineAndColumn(max(lStartPt, lEndPt), &span.iEndLine, &span.iEndIndex)))
		{
			if (howToShow == HowToShow::ShowCentered)
			{
				return SUCCEEDED(m_IVsTextView->CenterLines(span.iStartLine, span.iEndLine));
			}
			else if (howToShow == HowToShow::ShowTop)
			{
				return SUCCEEDED(m_IVsTextView->SetTopLine(span.iStartLine));
			}
			else if (howToShow == HowToShow::ShowAsIs)
			{
				return SUCCEEDED(m_IVsTextView->EnsureSpanVisible(span));
			}

			long top_line = -1, bottom_line = -1;
			GetVisibleLineRange(top_line, bottom_line);

			if (top_line == -1 || bottom_line == -1)
				return SUCCEEDED(m_IVsTextView->EnsureSpanVisible(span));
			else
			{
				int lines_visible = bottom_line - top_line + 1;
				int lines_span = span.iEndLine - span.iStartLine + 1;

				if (lines_visible >= lines_span)
				{
					return SUCCEEDED(m_IVsTextView->EnsureSpanVisible(span));
				}
				else
				{
					return SUCCEEDED(m_IVsTextView->SetTopLine(span.iStartLine));
				}
			}
		}

		return false;
	}

#pragma endregion IEdCnt

  private:
	virtual void GetPos(long& l, long& c)
	{
		_ASSERTE(gShellAttr->IsDevenv() && !gShellAttr->IsDevenv10OrHigher());
		CPoint cpt;
		SendVamMessage(VAM_CARETPOSITION, (WPARAM)&cpt, (LPARAM)&cpt);
		l = cpt.y;
		c = cpt.x;
	}
};
