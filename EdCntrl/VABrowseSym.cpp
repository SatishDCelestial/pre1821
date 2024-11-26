// VABrowseSym.cpp : implementation file
//

#include "stdafxed.h"
#include "resource.h"
#include "VABrowseSym.h"
#include "edcnt.h"
#include "VACompletionBox.h"
#include "VAFileView.h"
#include "FilterableSet.h"
#include "fdictionary.h"
#include "MenuXP\Tools.h"
#include "WindowUtils.h"
#include "expansion.h"
#include "VARefactor.h"
#include "Mparse.h"
#include "VaService.h"
#include "FindReferences.h"
#include "project.h"
#include "FileTypes.h"
#include "Registry.h"
#include "RegKeys.h"
#include "VAAutomation.h"
#include "ColorSyncManager.h"
#include "StringUtils.h"
#include "ImageListManager.h"
#include <Strsafe.h>
#include "FILE.H"
#if defined(RAD_STUDIO) && defined(_DEBUG)
#include "VaOptions.h"
#include "VAOpenFile.h"
#endif
#include "VAWatermarks.h"

#pragma warning(disable : 4505) // vectimp inlines not used

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static CStringW sFsisFilterString;
const int kThreadFilterTimerId = 200;
static bool gRedisplayFsis = false;

#define ID_REFACTOR_FINDREFS 0xE150
#define ID_TOGGLE_EDITOR_SELECTION_PREPOPULATE 0xE151
#define ID_TOGGLE_CASE_SENSITIVITY 0xe152
#define ID_TOGGLE_TOOLTIPS 0xe153
#define ID_TOGGLE_EXTRA_COLUMNS 0xe154
#if defined(RAD_STUDIO)
#define ID_OPENFILE_DLG 0xe155
#define ID_VA_OPTIONS 0xe156
#endif


/////////////////////////////////////////////////////////////////////////////
// VABrowseSym dialog

void VABrowseSymDlg()
{
#if defined(RAD_STUDIO) && defined(_DEBUG)
	// VABrowseSymDlg context menu short circuit for testing before there is a general facility 
	// for adding commands to RadStudio IDE
	EdCntPtr ed = g_currentEdCnt;
	if ((GetKeyState(VK_SHIFT) & 0x1000) && ed)
	{
		extern void ShowVAContextMenu();
		ShowVAContextMenu();
		return;
	}
#endif

	do
	{
		VABrowseSym dlg(gRedisplayFsis);
		gRedisplayFsis = false;
		dlg.DoModal();
	} while (gRedisplayFsis);
}

VABrowseSym::VABrowseSym(bool isRedisplay, CWnd* pParent /*=NULL*/)
    : FindInWkspcDlg(Psettings->mFsisExtraColumns ? "SiWDlgEx" : "SiWDlg", VABrowseSym::IDD, pParent, true, true),
      m_data(128000), mLoader(FsisLoader, this, "FsisLoader", false, false), mFilterEdited(false), mStopThread(false),
      mIsRedisplayed(isRedisplay)
{
	SetHelpTopic("dlgFsis");

	if (gTestsActive)
		mSymbolFilter = FilteredStringList::stfAll;
	else
	{
		const CString valueName(GetBaseRegName() + "TypeFilter");
		mSymbolFilter = (FilteredStringList::SymbolTypeFilter)GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, valueName,
		                                                                  FilteredStringList::stfAll);
	}
	//{{AFX_DATA_INIT(VABrowseSym)
	//}}AFX_DATA_INIT
}

void VABrowseSym::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(VABrowseSym)
	int checked = mSymbolFilter == FilteredStringList::stfOnlyTypes;
	DDX_Check(pDX, IDC_ONLY_SHOW_TYPES, checked);
	//}}AFX_DATA_MAP
	if (pDX->m_bSaveAndValidate)
	{
		if (checked)
			mSymbolFilter = FilteredStringList::stfOnlyTypes;
		else
			mSymbolFilter = FilteredStringList::stfAll;

		if (!gTestsActive)
		{
			const CString valueName(GetBaseRegName() + "TypeFilter");
			SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, valueName, mSymbolFilter);
		}
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VABrowseSym, FindInWkspcDlg)
//{{AFX_MSG_MAP(VABrowseSym)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
ON_WM_TIMER()
ON_WM_DESTROY()
ON_NOTIFY(LVN_GETDISPINFO, IDC_ITEMLIST, OnGetdispinfo)
ON_NOTIFY(LVN_GETDISPINFOW, IDC_ITEMLIST, OnGetdispinfoW)
ON_COMMAND(ID_EDIT_COPY, OnCopy)
ON_COMMAND(ID_REFACTOR_FINDREFS, OnRefactorFindRefs)
ON_COMMAND(ID_TOGGLE_EDITOR_SELECTION_PREPOPULATE, OnToggleEditorSelectionOption)
ON_COMMAND(ID_TOGGLE_CASE_SENSITIVITY, OnToggleCaseSensitivity)
ON_COMMAND(ID_TOGGLE_TOOLTIPS, OnToggleTooltips)
ON_COMMAND(ID_TOGGLE_EXTRA_COLUMNS, OnToggleExtraColumns)
#if defined(RAD_STUDIO) && defined(_DEBUG)
ON_COMMAND(ID_OPENFILE_DLG, OnOpenFileDlg)
ON_COMMAND(ID_VA_OPTIONS, OnVaOptions)
#endif
ON_BN_CLICKED(IDC_ONLY_SHOW_TYPES, OnToggleTypesFilter)
ON_WM_SHOWWINDOW()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

IMPLEMENT_MENUXP(VABrowseSym, FindInWkspcDlg);
/////////////////////////////////////////////////////////////////////////////
// VABrowseSym message handlers

void VABrowseSym::OnOK()
{
	StopThreadAndWait();
	FindInWkspcDlg::OnOK();
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
	{
		const int itemRow = (int)(intptr_t)p - 1;
		LPCTSTR pItem = m_data.GetAt(itemRow);
		const WTString record(::GetNlDelimitedRecord(pItem));
		token t = record;
		t.ReplaceAll(".", ":");
		WTString sym = t.read(DB_FIELD_DELIMITER_STR);
		CStringW file;
		int line;
		::GetDsItemSymLocation(pItem, file, line);
		// 		if (file.IsEmpty() && g_currentEdCnt)
		// 		{
		//			// added this for .net symbols, but seems ridiculous to wait
		//			// for FSIS and then wait for browser afterwards
		// 			WTString browseCmd(WTString("View.ObjectBrowserSearch ") + CleanScopeForDisplay(sym));
		// 			g_currentEdCnt->SendVamMessage(VAM_EXECUTECOMMAND, (long)browseCmd.c_str(), 0);
		// 		}
		// 		else
		if (!file.IsEmpty() && ::DelayFileOpen(file, line, StrGetSym(sym)))
		{
			WTString jnk;
			// check to see if sym is sufficient for MRU by doing what GoToDef
			// does without actually loading file.
			if (::GetGoToDEFLocation(sym.c_str(), file, line, jnk))
			{
				// this will not necessarily go to same place that
				// GetDsItemSymLocation went to if there were dupes.
				// The MRU doesn't handle dupes.
				::VAProjectAddScopeMRU(sym, mruMethodNav);
			}
		}
		else
		{
			// the old way of doing things
			if (::GoToDEF(sym))
				::VAProjectAddScopeMRU(sym, mruMethodNav);
			else
			{
				CStringW file2;
				int line2;
				GetItemLocation(itemRow, file2, line2);
				if (file2.IsEmpty())
					return;
				::DelayFileOpen(file2, line2, StrGetSym(sym));
			}
		}
	}
}

void VABrowseSym::PopulateList()
{
	SetWindowText("Find Symbol [loading...]");
	VAUpdateWindowTitle(VAWindowType::FSIS, *this, 0);
	if (mLoader.HasStarted() && !mLoader.IsFinished())
		StopThreadAndWait();

	m_data.SetSymbolTypes(mSymbolFilter);
	mLoader.StartThread();

	// wait until thread has started so that UpdateFilter logic works properly
	while (!mLoader.HasStarted())
		Sleep(10);

	UpdateFilter();
}

BOOL VABrowseSym::OnInitDialog()
{
	// should be added before FindInWkspcDlg::OnInitDialog because it invokes initialisation
	if (CVS2010Colours::IsExtendedThemeActive())
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_ONLY_SHOW_TYPES, this);
	FindInWkspcDlg::OnInitDialog();

	// make list control unicode
	mFilterList.SendMessage(CCM_SETUNICODEFORMAT, 1, 0);

	AddSzControl(IDC_ITEMLIST, mdResize, mdResize);
	AddSzControl(IDC_FILTEREDIT, mdResize, mdRepos);
	AddSzControl(IDC_ALL_OPEN_FILES, mdNone, mdRepos);
	AddSzControl(IDC_ONLY_SHOW_TYPES, mdNone, mdRepos);
	AddSzControl(IDC_USE_FUZZY_SEARCH, mdNone, mdRepos);
	AddSzControl(IDOK, mdRepos, mdRepos);
	AddSzControl(IDCANCEL, mdRepos, mdRepos);

	CRect rc;
	mFilterList.GetClientRect(&rc);

	int width;
	width = GetInitialColumnWidth(0, Psettings->mFsisExtraColumns ? (int)(rc.Width() * 0.35) : rc.Width() / 3);
	mFilterList.InsertColumn(0, "Symbol", LVCFMT_LEFT, width);

	width = GetInitialColumnWidth(1, Psettings->mFsisExtraColumns ? (int)(rc.Width() * 0.35)
	                                                              : rc.Width() - (rc.Width() / 3));
	mFilterList.InsertColumn(1, "Definition", LVCFMT_LEFT, width);

	if (Psettings->mFsisExtraColumns)
	{
		width = GetInitialColumnWidth(2, (int)(rc.Width() * 0.2));
		mFilterList.InsertColumn(2, "File", LVCFMT_LEFT, width);
		::mySetProp(mFilterList.m_hWnd, "__VA_do_not_colourCol2", (HANDLE)1);

		width = GetInitialColumnWidth(3, (int)(rc.Width() * 0.5));
		mFilterList.InsertColumn(3, "Directory", LVCFMT_LEFT, width);
		::mySetProp(mFilterList.m_hWnd, "__VA_do_not_colourCol3", (HANDLE)1);
	}

	if (sFsisFilterString.GetLength())
	{
		mEdit.SetText(sFsisFilterString);
		mEdit.SetSel(0, -1);
	}

	SetTooltipStyle(Psettings->mFsisTooltips);

	if (!mIsRedisplayed && g_currentEdCnt && Psettings && Psettings->mFindSymbolInSolutionUsesEditorSelection)
	{
		// [case: 71460] after inserting previous filter, update filter based on editor
		// selection.  ctrl+z will restore previous filter.
		WTString selTxt(g_currentEdCnt->GetSelString());
		selTxt.Trim();
		if (!selTxt.IsEmpty() && -1 == selTxt.FindOneOf("\r\n"))
		{
			mEdit.SetSel(0, -1);
			// Use ReplaceSel so that undo works
			::SendMessageW(mEdit.m_hWnd, EM_REPLACESEL, (WPARAM)TRUE, (LPARAM)(LPCWSTR)selTxt.Wide());
			mEdit.SetSel(0, -1);
		}
	}

	PopulateList();
	mFilterEdited = false;

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void VABrowseSym::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kThreadFilterTimerId)
	{
		if (mLoader.HasStarted() && !mLoader.IsFinished())
			return;

		KillTimer(nIDEvent);
		UpdateFilter();
	}
	else
		__super::OnTimer(nIDEvent);
}

#if defined(RAD_STUDIO) && defined(_DEBUG)
void VABrowseSym::OnOpenFileDlg()
{
	OnCancel();
	::VAOpenFileDlg();
}

void VABrowseSym::OnVaOptions()
{
	OnCancel();
	::DoVAOptions(nullptr);
}
#endif

void VABrowseSym::UpdateFilter(bool force /*= false*/)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		KillTimer(kThreadFilterTimerId);
		SetTimer(kThreadFilterTimerId, 50, NULL);
		return;
	}

	CWaitCursor curs;
	const CStringW previousFilter(sFsisFilterString);
	mEdit.GetText(sFsisFilterString);
	if (previousFilter != sFsisFilterString)
		mFilterEdited = true;
	uint32_t t1 = ::GetTickCount();
	m_data.Filter(WTString(sFsisFilterString), IsFuzzyUsed(), force);
	uint32_t t2 = ::GetTickCount();
	if (mFilterList.GetItemCount() < 50000)
	{
		// when list is very large, deleteAllItems has horrible performance.
		// DeleteAllItems is required for ScrollItemToMiddle to work for letters
		// beyond the first typed.
		// ScrollItemToMiddle not functional until item count is < xxK items.
		// case=20182
		mFilterList.DeleteAllItems();
	}
	const int kSetCount = m_data.GetFilteredSetCount();
	mFilterList.SetItemCount(kSetCount);
	gImgListMgr->SetImgList(mFilterList, ImageListManager::bgList, LVSIL_SMALL);
	mFilterList.Invalidate();
	if (kSetCount)
	{
		POSITION p = mFilterList.GetFirstSelectedItemPosition();
		// clear selection
		for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
		{
			mFilterList.SetItemState((int)(intptr_t)p - 1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			mFilterList.GetNextSelectedItem(p);
		}
		const int kSuggestedItem = m_data.GetSuggestionIdx();
		mFilterList.SetItemState(kSuggestedItem, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
		// [case: 19262] scroll to middle if suggestion is beyond middle
		ScrollItemToMiddle(kSuggestedItem);
	}
	uint32_t t3 = ::GetTickCount();

	UpdateTitle(t2 - t1, t3- t2);
}

// Windows is asking for the text to display, at item(iItemIndex), subitem(pItem->iSubItem)
void VABrowseSym::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;

	LV_ITEM* pItem = &(pDispInfo)->item;
	int iItemIndex = pItem->iItem;
	LPCSTR p = m_data.GetAt(iItemIndex);

	if (p && ((pItem->mask & LVIF_TEXT) == LVIF_TEXT || (pItem->mask & LVIF_IMAGE) == LVIF_IMAGE))
	{
		switch (pItem->iSubItem)
		{
		case 0:
			if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
			{
				if (*p == ':')
					p++;

				int spos = 0;
				for (int i = 0; i < pItem->cchTextMax && p[i] && p[i] != DB_FIELD_DELIMITER; i++)
				{
					char c = DecodeChar(p[i]);
					if (c == ':' || c == '.')
						pItem->pszText[spos++] = '.';
					//						spos = 0;
					else
						pItem->pszText[spos++] = c;
				}

				pItem->pszText[spos] = '\0';
				const WTString tmp(::CleanScopeForDisplay(pItem->pszText));
				::StringCchCopy(pItem->pszText, (uint)pItem->cchTextMax, tmp.c_str());
			}

			if ((pItem->mask & LVIF_IMAGE) == LVIF_IMAGE)
			{
				uint type, attrs;
				::GetDsItemTypeAndAttrsAfterText(p, type, attrs);
				pItem->iImage = GetTypeImgIdx(type, attrs);
			}
			break;
		case 1:
			p = strchr(++p, DB_FIELD_DELIMITER);
			if (p)
			{
				p++;
				int i;
				for (i = 0; i < pItem->cchTextMax && p[i] && p[i] != DB_FIELD_DELIMITER; i++)
					pItem->pszText[i] = DecodeChar(p[i]);
				pItem->pszText[i] = '\0';
				const WTString tmp(::CleanDefForDisplay(pItem->pszText, gTypingDevLang));
				::StringCchCopy(pItem->pszText, (uint)pItem->cchTextMax, tmp.c_str());
			}
			break;
		case 2:
			if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
			{
				CStringW f;
				int ln;
				GetItemLocation(iItemIndex, f, ln);
				f = ::Basename(f);
				WTString ff(f);
				::StringCchCopy(pItem->pszText, (uint)pItem->cchTextMax, ff.c_str());
			}
			break;
		case 3:
			if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
			{
				CStringW f;
				int ln;
				GetItemLocation(iItemIndex, f, ln);
				f = ::Path(f);
				WTString ff(f);
				::StringCchCopy(pItem->pszText, (uint)pItem->cchTextMax, ff.c_str());
			}
			break;
		}
	}

	*pResult = 0;
}

void VABrowseSym::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (mLoader.HasStarted() && !mLoader.IsFinished())
	{
		// return empty if thread is still running
		*pResult = 0;
		return;
	}

	LV_DISPINFOW* pDispInfo = (LV_DISPINFOW*)pNMHDR;

	LV_ITEMW* pItem = &(pDispInfo)->item;
	int iItemIndex = pItem->iItem;
	LPCSTR p = m_data.GetAt(iItemIndex);

	CStringA astr;

	const int tmpBufLen = pItem->cchTextMax * 2;
	LPSTR pszText = nullptr;
	const int cchTextMax = pItem->cchTextMax;

	if (p && ((pItem->mask & LVIF_TEXT) == LVIF_TEXT || (pItem->mask & LVIF_IMAGE) == LVIF_IMAGE))
	{
		switch (pItem->iSubItem)
		{
		case 0:
			if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
			{
				if (*p == ':')
					p++;

				pszText = astr.GetBuffer(tmpBufLen);

				int spos = 0;
				for (int i = 0; i < cchTextMax && p[i] && p[i] != DB_FIELD_DELIMITER; i++)
				{
					char c = DecodeChar(p[i]);
					if (c == ':' || c == '.')
						pszText[spos++] = '.';
					else
						pszText[spos++] = c;
				}

				pszText[spos] = '\0';
				const WTString tmp(::CleanScopeForDisplay(pszText));
				::StringCchCopy(pszText, (uint)tmpBufLen, tmp.c_str());

				astr.ReleaseBufferSetLength(strlen_i(pszText));
				CStringW wstr = ::MbcsToWide(astr, astr.GetLength());
				::StringCchCopyW(pItem->pszText, (uint)pItem->cchTextMax, wstr);
			}

			if ((pItem->mask & LVIF_IMAGE) == LVIF_IMAGE)
			{
				uint type, attrs;
				::GetDsItemTypeAndAttrsAfterText(p, type, attrs);
				pItem->iImage = GetTypeImgIdx(type, attrs);
			}
			break;

		case 1:
			if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
			{
				p = strchr(++p, DB_FIELD_DELIMITER);
				if (p)
				{
					p++;
					pszText = astr.GetBuffer(tmpBufLen);
					int i;
					for (i = 0; i < cchTextMax && p[i] && p[i] != DB_FIELD_DELIMITER; i++)
						pszText[i] = DecodeChar(p[i]);

					pszText[i] = '\0';
					const WTString tmp(::CleanDefForDisplay(pszText, gTypingDevLang));
					::StringCchCopy(pszText, (uint)tmpBufLen, tmp.c_str());

					astr.ReleaseBufferSetLength(strlen_i(pszText));
					CStringW wstr = ::MbcsToWide(astr, astr.GetLength());
					::StringCchCopyW(pItem->pszText, (uint)pItem->cchTextMax, wstr);
				}
			}
			break;

		case 2:
			if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
			{
				CStringW f;
				int ln;
				GetItemLocation(iItemIndex, f, ln);
				f = ::Basename(f);
				::StringCchCopyW(pItem->pszText, (uint)pItem->cchTextMax, f);
			}
			break;

		case 3:
			if ((pItem->mask & LVIF_TEXT) == LVIF_TEXT)
			{
				CStringW f;
				int ln;
				GetItemLocation(iItemIndex, f, ln);
				f = ::Path(f);
				::StringCchCopyW(pItem->pszText, (uint)pItem->cchTextMax, f);
			}
			break;
		}
	}

	if ((pItem->mask & LVIF_INDENT) == LVIF_INDENT)
		pItem->iIndent = 0;

	*pResult = 0;
}

void VABrowseSym::OnDestroy()
{
	StopThreadAndWait();
	mEdit.GetText(sFsisFilterString);
	FindInWkspcDlg::OnDestroy();
}

void VABrowseSym::GetItemLocation(int itemRow, CStringW& file, int& lineNo)
{
	LPCSTR pItem = m_data.GetAt(itemRow);
	::GetDsItemSymLocation(pItem, file, lineNo);
	if (!file.IsEmpty())
		return;

	const WTString record(::GetNlDelimitedRecord(pItem));
	token t = record;
	WTString tmp = t.read(DB_FIELD_DELIMITER_STR);
	tmp.ReplaceAll(".", ":");
	lineNo = -1;
	file = ::GetSymbolLoc(tmp, lineNo);
}

void VABrowseSym::GetItemLocation(int itemRow, CStringW& file, int& lineNo, WTString& sym)
{
	LPCSTR pItem = m_data.GetAt(itemRow);
	::GetDsItemSymLocation(pItem, file, lineNo);
	const WTString record(::GetNlDelimitedRecord(pItem));
	token t = record;
	sym = t.read(DB_FIELD_DELIMITER_STR);
	sym.ReplaceAll(".", ":");

	if (file.IsEmpty())
	{
		lineNo = -1;
		file = ::GetSymbolLoc(sym, lineNo);
	}
}

void VABrowseSym::GetTooltipText(int itemRow, WTString& txt)
{
	CStringW tmp;
	GetTooltipTextW(itemRow, tmp);
	txt = tmp;
}

void VABrowseSym::GetTooltipTextW(int itemRow, CStringW& txt)
{
	CStringW file;
	int line;
	WTString sym;
	GetItemLocation(itemRow, file, line, sym);
	txt = mFilterList.GetItemTextW(itemRow, 0) + L"\n\n";
	CStringW comment = GetCommentForSym(sym);

	if (!comment.IsEmpty())
	{
		comment.TrimLeft();
		txt += comment + L'\n';
	}

	CStringW decl = mFilterList.GetItemTextW(itemRow, 1);
	decl.Replace(L"{...}", L"");
	txt += decl;

	if (!file.IsEmpty())
	{
		if (gTestsActive && gTestLogger && gTestLogger->IsTooltipLoggingEnabled())
		{
			CStringW tmp = txt;
			CString__AppendFormatW(tmp, L"\n\nFile: %s:%d", Basename(file).GetBuffer(), line);
			gTestLogger->LogStrW(tmp);
		}

		CString__AppendFormatW(txt, L"\n\nFile: %s:%d", file.GetBuffer(), line);
	}
}

void VABrowseSym::UpdateTitle(uint64_t timing_filter_ms, uint64_t timing_listview_ms)
{
	CString title;
	CString__FormatA(title, "Find Symbol [%d of %d]", m_data.GetFilteredSetCount(), m_data.GetCount());
	#ifdef _DEBUG
	title.AppendFormat(": filter(%llums) listview(%llums)", timing_filter_ms, timing_listview_ms);
	#endif
	VAUpdateWindowTitle(VAWindowType::FSIS, title, 1);
	SetWindowText(title);
}

int VABrowseSym::GetFilterTimerPeriod() const
{
	const int kWorkingSetCount = GetWorkingSetCount();
	if (kWorkingSetCount < 1000)
		return 0;
	else if (kWorkingSetCount < 10000)
		return 50;
	else if (kWorkingSetCount < 25000)
		return 100;
	else if (kWorkingSetCount < 50000)
		return 200;
	else if (kWorkingSetCount < 100000)
		return 300;
	else if (kWorkingSetCount < 200000)
		return 400;
	else if (kWorkingSetCount < 500000)
		return 600;
	else
		return 750;
}

void VABrowseSym::OnCopy()
{
	CStringW clipTxt;

	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
	{
		const int itemRow = (int)(intptr_t)p - 1;

		clipTxt += CStringW(mFilterList.GetItemText(itemRow, 0));
		if (n > 1)
			clipTxt += L"\r\n";
	}

	if (clipTxt.GetLength() && g_currentEdCnt)
		::SaveToClipboard(m_hWnd, clipTxt);
}

void VABrowseSym::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	uint selItemCnt = mFilterList.GetSelectedCount();

	_ASSERTE((selItemCnt > 0 && p) || (!selItemCnt && !p));

	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && p)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (mFilterList.GetItemRect((int)(intptr_t)p - 1, &rc, LVIR_ICON))
		{
			mFilterList.ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}
	else if (!selItemCnt && !p && pos == CPoint(-1, -1))
	{
		// [case: 78216] nothing selected and not a click
		mFilterList.GetClientRect(&rc);
		mFilterList.ClientToScreen(&rc);
		pos.x = rc.left + 16;
		pos.y = rc.top + 16;
	}

	const DWORD kSelectionDependentItemState = (p) ? 0u : MF_GRAYED | MF_DISABLED;

	CMenu contextMenu;
	contextMenu.CreatePopupMenu();
	contextMenu.AppendMenu(kSelectionDependentItemState | MF_DEFAULT, MAKEWPARAM(IDOK, ICONIDX_REFERENCE_GOTO_DEF),
	                       "&Goto");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_COPY, ICONIDX_COPY), "&Copy\tCtrl+C");

	GetDataForCurrentSelection();
	if (mDtypes.size())
	{
		DType sym(*mDtypes.begin());
		const BOOL kCanFindRefs = VARefactorCls::CanFindReferences(&sym);
		if (kCanFindRefs)
			contextMenu.AppendMenu(0, MAKEWPARAM(ID_REFACTOR_FINDREFS, ICONIDX_REFERENCE_FIND_REF),
			                       "Find &References\tCtrl+F");
	}

#if defined(RAD_STUDIO) && defined(_DEBUG)
	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(0, ID_OPENFILE_DLG, "&Open File in Project Group");
	contextMenu.AppendMenu(0, ID_VA_OPTIONS, "&VA Options");
#endif

	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(Psettings->mFsisTooltips ? MF_CHECKED : 0u, ID_TOGGLE_TOOLTIPS, "&Tooltips");
	contextMenu.AppendMenu(Psettings->mFsisExtraColumns ? MF_CHECKED : 0u, ID_TOGGLE_EXTRA_COLUMNS,
	                       "&File columns (reopens dialog)");

	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(Psettings->mFindSymbolInSolutionUsesEditorSelection ? MF_CHECKED : 0u,
	                       ID_TOGGLE_EDITOR_SELECTION_PREPOPULATE, "&Initialize search with editor selection");
	// command text corresponds to inverted setting
	contextMenu.AppendMenu(UINT(Psettings->mForceCaseInsensitiveFilters ? MF_ENABLED : MF_ENABLED | MF_CHECKED),
	                       MAKEWPARAM(ID_TOGGLE_CASE_SENSITIVITY, 0),
	                       "&Match case of search strings that contain uppercase letters");

	CMenuXP::SetXPLookNFeel(this, contextMenu);

	MenuXpHook hk(this);
	contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
}

BOOL VABrowseSym::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->hwnd == mFilterList.m_hWnd || pMsg->hwnd == mEdit.m_hWnd)
	{
		if (pMsg->message == WM_KEYDOWN)
		{
			if ((GetKeyState(VK_CONTROL) & 0x1000))
			{
				if (pMsg->wParam == 'C')
				{
					// Ctrl+C
					if (pMsg->hwnd == mEdit.m_hWnd)
					{
						// [case: 65917]
						// don't handle as OnCopy() if edit ctrl has selected text
						int p1, p2;
						p1 = p2 = 0;
						mEdit.GetSel(p1, p2);
						if (p1 != p2)
							return __super::PreTranslateMessage(pMsg);
					}

					OnCopy();
					return false;
				}

				if (pMsg->wParam == 'F')
				{
					// Ctrl+F
					OnRefactorFindRefs();
					return false;
				}

				if (pMsg->wParam == 'A' && pMsg->hwnd == mEdit.m_hWnd)
				{
					// Ctrl+A
					// I was getting some strange behavior with default Ctrl+A behavior
					// caret in strange position and if text had a '-' sometimes GetWindowText
					// would come back empty.
					mEdit.SetSel(0, -1);
					return false;
				}
			}
		}
	}

	return __super::PreTranslateMessage(pMsg);
}

void VABrowseSym::OnRefactorFindRefs()
{
	GetDataForCurrentSelection();
	if (gVaService && mDtypes.size())
	{
		OnCancel();
		DType sym(*mDtypes.begin());
		gVaService->FindReferences(FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments | FREF_FLG_FindAutoVars,
		                           GetTypeImgIdx(sym.MaskedType(), sym.Attributes()), sym.SymScope());
		mDtypes.clear();
	}
}

void VABrowseSym::GetDataForCurrentSelection()
{
	mDtypes.clear();
	int devLang = gTypingDevLang;
	if (devLang == -1)
		devLang = Src;
	else if (Is_Some_Other_File(devLang))
		devLang = Src; // [case: 39527] fix for find refs in FSIS while .txt file is active
	MultiParsePtr mp = MultiParse::Create(devLang);
	POSITION p = mFilterList.GetFirstSelectedItemPosition();
	for (uint n = mFilterList.GetSelectedCount(); n > 0; n--)
	{
		const int itemRow = (int)(intptr_t)p - 1;
		LPCSTR pItem = m_data.GetAt(itemRow);
		const WTString record(::GetNlDelimitedRecord(pItem));
		token t = record;
		t.ReplaceAll(".", ":");
		WTString sym = t.read(DB_FIELD_DELIMITER_STR);
		DType* cd = mp->FindExact2(sym.c_str());
		if (cd)
		{
			DType d(cd);
			mDtypes.push_back(d);
		}
		else
		{
			// need search to include GOTODEFs
			mp->FindExactList(sym.c_str(), mDtypes, !IsRestrictedToWorkspace());
		}

		if (mDtypes.size())
			return;
	}
}

int VABrowseSym::GetWorkingSetCount() const
{
	CStringW newFilter;
	mEdit.GetText(newFilter);
	bool filterCurrentSet = false;
	if (m_data.GetFilteredSetCount() && sFsisFilterString.GetLength() &&
	    sFsisFilterString.GetLength() <= newFilter.GetLength() && newFilter.Find(sFsisFilterString) == 0 &&
	    newFilter.Find(L'-') == -1)
	{
		filterCurrentSet = true;
	}

	return filterCurrentSet ? m_data.GetFilteredSetCount() : m_data.GetCount();
}

void VABrowseSym::OnToggleTypesFilter()
{
	mEdit.SetFocus();
	if (mFilterEdited)
		mEdit.SetSel(-1, -1); // [case: 52462]
	UpdateData(TRUE);
	PopulateList();
}

void VABrowseSym::FsisLoader(LPVOID pVaBrowseSym)
{
	VABrowseSym* _this = (VABrowseSym*)pVaBrowseSym;
	_this->PopulateListThreadFunc();
	_this->mStopThread = false;
}

void VABrowseSym::PopulateListThreadFunc()
{
	if (mStopThread)
		return;

	if (!GlobalProject || (!GlobalProject->GetFileItemCount() && GlobalProject->SolutionFile().IsEmpty()))
	{
		m_data.Empty();
		return;
	}

	m_data.ReadDB(IsRestrictedToWorkspace());
	if (mStopThread)
		return;

	m_data.Sort(::FilteredSetStringCompareNoCase);
	if (mStopThread)
		return;

	m_data.RemoveDuplicates();
}

void VABrowseSym::StopThreadAndWait()
{
	if (!mLoader.IsFinished())
	{
		mStopThread = true;
		CWaitCursor curs;
		if (::GetCurrentThreadId() == g_mainThread)
			mLoader.Wait(10000);
		else
			mLoader.Wait(INFINITE);
	}
}

void VABrowseSym::OnToggleEditorSelectionOption()
{
	Psettings->mFindSymbolInSolutionUsesEditorSelection = !Psettings->mFindSymbolInSolutionUsesEditorSelection;
	if (g_currentEdCnt && Psettings->mFindSymbolInSolutionUsesEditorSelection)
	{
		// [case: 71460] after inserting previous filter, update filter based on editor
		// selection.  ctrl+z will restore previous filter.
		WTString selTxt(g_currentEdCnt->GetSelString());
		selTxt.Trim();
		if (!selTxt.IsEmpty() && -1 == selTxt.FindOneOf("\r\n"))
		{
			mEdit.SetSel(0, -1);
			// Use ReplaceSel so that undo works
			::SendMessageW(mEdit.m_hWnd, EM_REPLACESEL, (WPARAM)TRUE, (LPARAM)(LPCWSTR)selTxt.Wide());
			mEdit.SetSel(0, -1);
		}
	}
}

void VABrowseSym::OnToggleCaseSensitivity()
{
	Psettings->mForceCaseInsensitiveFilters = !Psettings->mForceCaseInsensitiveFilters;
	SetEditHelpText();
	UpdateFilter(true);
}

void VABrowseSym::OnToggleTooltips()
{
	Psettings->mFsisTooltips = !Psettings->mFsisTooltips;
	SetTooltipStyle(Psettings->mFsisTooltips);
}

void VABrowseSym::OnToggleExtraColumns()
{
	Psettings->mFsisExtraColumns = !Psettings->mFsisExtraColumns;
	gRedisplayFsis = true;
	OnCancel();
}
