// FileListCombo.cpp : implementation file
//

#include "stdafxed.h"
#include "edcnt.h"
#include "expansion.h"
#include "filelist.h"
#include "Project.h"
#include "FileListCombo.h"
#include "VAFileView.h"
#include "file.h"
#include "DevShellAttributes.h"
#include "WindowUtils.h"
#include "ColorListControls.h"
#include "StringUtils.h"
#include "VAWorkspaceViews.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// This class only holds filter matched items.
// The source items are pulled from the GlobalProject each pass through the filter.
class ProjectFileSet
{
	CStringW mFilter;
	FileList mFilteredSet;
	int mFilteredSetCount;
	int mSuggestedItemIdx;

	void AddItem(const FileInfo& item)
	{
		mFilteredSet.Add(item);
		mFilteredSetCount++;
	}

  public:
	ProjectFileSet() : mFilteredSetCount(0), mSuggestedItemIdx(0)
	{
	}

	void Filter(const CStringW& newFilter)
	{
		Empty();
		if (!GlobalProject->SortFileListIfReady())
			return;
		mFilter = StrMatchOptions::TweakFilter(newFilter);
		StrMatchOptions opts(mFilter);

		const int kFilterLen = mFilter.GetLength();
		const bool kIsMultiPattern = StrIsMultiMatchPattern(mFilter);
		int matchRank = -1;
		CStringW curProjItem;
		RWLockReader lck;
		const FileList& files = GlobalProject->GetFilesSortedByName(lck);
		for (FileList::const_iterator it = files.begin(); it != files.end(); ++it)
		{
			curProjItem = (*it).mFilename;
			if (kFilterLen)
			{
				int smatch = kIsMultiPattern ? ::StrMultiMatchRanked(::GetBaseName(curProjItem), mFilter, false)
				                             : ::StrMatchRankedW(::GetBaseName(curProjItem), opts, false);
				if (smatch)
				{
					if (matchRank < smatch)
					{
						matchRank = smatch;
						mSuggestedItemIdx = mFilteredSetCount;
					}
					AddItem(*it);
				}
			}
			else
				AddItem(*it);
		}
	}

	void Empty()
	{
		mSuggestedItemIdx = mFilteredSetCount = 0;
		mFilter.Empty();
		mFilteredSet.clear();
	}

	int GetCount() const
	{
		return GlobalProject->GetFileItemCount();
	}
	int GetFilteredSetCount() const
	{
		return mFilteredSetCount;
	}
	int GetSuggestionIdx() const
	{
		return mSuggestedItemIdx;
	}

	CStringW GetAt(int n)
	{
		if (n >= 0 && n < mFilteredSetCount)
		{
			FileList::const_iterator it = mFilteredSet.begin();
			for (int itIdx = 0; it != mFilteredSet.end() && itIdx < n; ++it, ++itIdx)
				;

			if (it == mFilteredSet.end())
				return CStringW();
			return (*it).mFilename;
		}
		_ASSERTE(n < mFilteredSetCount || (!mFilteredSetCount && !n));
		return CStringW();
	}
};

static ProjectFileSet sFileData;

/////////////////////////////////////////////////////////////////////////////
// FileListCombo

static WTString kDefaultTitle;
static CStringW kDefaultTitleW;

FileListCombo::FileListCombo() : CFastComboBox(false)
{
	sFileData.Empty();
	if (kDefaultTitle.IsEmpty())
	{
		if (gShellAttr->IsMsdev())
		{
			kDefaultTitle = "[Files in Workspace]";
			kDefaultTitleW = L"[Files in Workspace]";
		}
		else
		{
			kDefaultTitleW = "[Files in Solution]";
			kDefaultTitle = L"[Files in Solution]";
		}
	}
}

FileListCombo::~FileListCombo()
{
}

BEGIN_MESSAGE_MAP(FileListCombo, CFastComboBox)
//{{AFX_MSG_MAP(FileListCombo)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void FileListCombo::Init()
{
	CFastComboBox::Init();
	SetItemCount(1);
	SetItem(kDefaultTitle.c_str(), ICONIDX_FIW);
}

/////////////////////////////////////////////////////////////////////////////
// FileListCombo message handlers
WTString FileListCombo::GetItemTip(int nRow) const
{
	return WTString(GetItemTipW(nRow));
}

CStringW FileListCombo::GetItemTipW(int nRow) const
{
	CStringW file;
	if (GetDroppedState())
		file = sFileData.GetAt(nRow);
	return file;
}

void FileListCombo::UpdateFilter(const CStringW& txt)
{
	sFileData.Filter(txt);
	SetItemCount(sFileData.GetFilteredSetCount());
}

bool FileListCombo::IsVS2010ColouringActive() const
{
	return CVS2010Colours::IsVS2010VAViewColouringActive();
}

void FileListCombo::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!sFileData.GetCount())
		UpdateFilter(L"");

	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
	LV_ITEM* pItem = &(pDispInfo)->item;

	if (pItem->mask & LVIF_TEXT)
	{
		pItem->pszText[0] = '\0';
		pItem->iImage = 0;
	}

	if (sFileData.GetCount() && sFileData.GetFilteredSetCount())
	{
		int iItemIndex = pItem->iItem;
		const CStringW file = ::GetBaseName(sFileData.GetAt(iItemIndex));
		if (file.GetLength())
		{
			if (pItem->mask & LVIF_TEXT)
				::_tcscpy(pItem->pszText, WTString(file).c_str());
			pItem->iImage = ::GetFileImgIdx(file);
		}
	}

	*pResult = 0;
}

void FileListCombo::OnGetdispinfoW(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!sFileData.GetCount())
		UpdateFilter(L"");

	LV_DISPINFOW* pDispInfo = (LV_DISPINFOW*)pNMHDR;
	LVITEMW* pItem = &(pDispInfo)->item;

	if (pItem->mask & LVIF_TEXT)
	{
		pItem->pszText[0] = L'\0';
		pItem->iImage = 0;
	}

	if (sFileData.GetCount() && sFileData.GetFilteredSetCount())
	{
		int iItemIndex = pItem->iItem;
		const CStringW file = ::GetBaseName(sFileData.GetAt(iItemIndex));
		if (file.GetLength())
		{
			if (pItem->mask & LVIF_TEXT)
				::wcscpy(pItem->pszText, file);
			pItem->iImage = ::GetFileImgIdx(file);
		}
	}

	*pResult = 0;
}

void FileListCombo::OnEditChange()
{
	if (!IsWindow())
		return;

	CStringW txt;
	if (GetEditCtrl())
		::GetWindowTextW(GetEditCtrl()->m_hWnd, txt);

	if (txt == kDefaultTitleW)
		return;

	POSITION pos = GetFirstSelectedItemPosition();
	const int nItem = GetNextSelectedItem(pos);

	const CStringW cursel = GetItemTextW(nItem, 1);
	if (!txt.GetLength() ||
	    (cursel != txt && (__iswcsym(txt[0]) || txt[0] == L'.' || txt[0] == L'-' || txt[0] == L',')))
	{
		UpdateFilter(txt == kDefaultTitleW ? CStringW() : txt);
	}
	SetItemState(sFileData.GetSuggestionIdx(), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	EnsureVisible(sFileData.GetSuggestionIdx(), FALSE);

	if (!GetDroppedState())
	{
		CRect rc;
		GetWindowRect(&rc);
		SetDroppedWidth((UINT)rc.Width());
		DisplayList(TRUE);
	}
}

void FileListCombo::OnDropdown()
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftFis);
	CWaitCursor curs;
	if (!IsWindow())
		Init();

	UpdateFilter(L"");

	CRect rc;
	GetWindowRect(&rc);
	SetDroppedWidth((UINT)rc.Width());
	ShowDropDown(FALSE);
	DisplayList(TRUE);

	SetItemState(sFileData.GetSuggestionIdx(), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	EnsureVisible(sFileData.GetSuggestionIdx(), FALSE);
}

void FileListCombo::OnSelect()
{
	POSITION pos = GetFirstSelectedItemPosition();
	const int nItem = GetNextSelectedItem(pos);

	const CStringW file = sFileData.GetAt(nItem);

	DisplayList(FALSE);
	if (file.GetLength())
	{
		LVITEM item;
		memset(&item, 0, sizeof(LVITEM));
		item.mask = LVIF_IMAGE;
		item.iItem = nItem;
		item.iSubItem = 0;
		GetItem(&item);
		DelayFileOpen(file, 0, nullptr);
		VAProjectAddFileMRU(file, mruFileSelected);
	}
}

void FileListCombo::GetDefaultTitleAndIconIdx(WTString& title, int& iconIdx) const
{
	title = kDefaultTitle;
	iconIdx = ICONIDX_FIW;
}

void FileListCombo::OnCloseup()
{
	if (GetItemCount() == 0)
	{
		CEdit* pEdit = GetEditCtrl();
		if (pEdit)
			pEdit->SetSel(0, -1);
	}
}
