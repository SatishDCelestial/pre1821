#include "StdAfxEd.h"
#include "ReferencesWndBase.h"
#include "expansion.h"
#include "DevShellAttributes.h"
#include "RedirectRegistryToVA.h"
#include "Settings.h"
#include "FindReferencesThread.h"
#include "mainThread.h"
#include "Lock.h"
#include "ImageListManager.h"
#include "MenuXP\MenuXP.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "SubClassWnd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

const uint WM_SHOW_PROGRESS = ::RegisterWindowMessage("WM_SHOW_PROGRESS");
const uint WM_ADD_REFERENCE = ::RegisterWindowMessage("WM_ADD_REFERENCE");
const uint WM_ADD_FILEREFERENCE = ::RegisterWindowMessage("WM_ADD_FILEREFERENCE");
const uint WM_ADD_PROJECTGROUP = ::RegisterWindowMessage("WM_ADD_PROJECTGROUP");
const UINT WM_FIND_COMPLETE = ::RegisterWindowMessage("WM_FIND_COMPLETE");
const UINT WM_FIND_PROGRESS = ::RegisterWindowMessage("WM_FIND_PROGRESS");
const UINT WM_HIDDEN_REFS = ::RegisterWindowMessage("WM_HIDDEN_REFS");
const DWORD kFileRefBit = 0x80000000;

// columns definitions
static const struct
{
	const char* name;
	int default_width;
	bool default_hidden;
} column_definitions[__column_countof] = {{"Reference", 300, false},
#ifdef ALLOW_MULTIPLE_COLUMNS
                                          {"Line", 80, false},
                                          {"Type", 80, false},
                                          {"Context", 200, false}
#endif
};

/////////////////////////////////////////////////////////////////////////////
// ReferencesThreadObserver

class ReferencesThreadObserver : public IFindReferencesThreadObserver
{
	ReferencesWndBase* mReferencesWnd;
	HTREEITEM mTreeItemForCurrentProject = TVI_ROOT;
	HTREEITEM mTreeItemForCurrentFile = TVI_ROOT;
	CColumnTreeCtrl* mReferencesTree;
	int mIconType;
	int mFileCount = 0;
	FindReferencesPtr mRefs;
	volatile bool mCanceled = false;
	const bool mHonorsDisplayTypeFilter;
	const DWORD mFilter;

	CStringW mCurrentProject;
	int mCurrentProjectRefId = 0;
	CStringW mCurrentFile;
	int mCurrentFileRefId = 0;
	int mHiddenReferences = 0;

  public:
	ReferencesThreadObserver(ReferencesWndBase* pDlgIn, int iconTypeIn, bool honorsIncludeAndUnknownFilter,
	                         DWORD filter)
	    : mReferencesWnd(pDlgIn), mIconType(iconTypeIn), mRefs(pDlgIn->mRefs),
	      mHonorsDisplayTypeFilter(honorsIncludeAndUnknownFilter), mFilter(filter)
	{
		mReferencesTree = &mReferencesWnd->m_tree;
	}

	virtual void OnCancel()
	{
		mCanceled = true;
	}

	void OnBeforeFlushReferences(size_t count) override
	{
		mReferencesWnd->m_tree.SetRedraw(false);
	}
	void OnAfterFlushReferences() override
	{
//		mReferencesWnd->EnsureLastItemVisible();
		mReferencesWnd->m_tree.SetRedraw(true);
		mReferencesWnd->m_tree.RedrawWindow(nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
	}

	virtual void OnReference(int refId)
	{
		if (mCanceled)
			return;

		WTString text;
		CStringW file;
		long pos;
		const int type = mRefs->GetFileLine(refId, file, pos, text, false);
		const FindReference* ref = mRefs->GetReference((uint)refId);
		if (ref)
		{
			if (!ref->ShouldDisplay())
				return;

			mHiddenReferences++;

			if (mHonorsDisplayTypeFilter)
			{
				// find refs results wnd
				if (ref->type == FREF_Comment && !(mFilter & (1 << FREF_Comment)))
					return;
				if (ref->type == FREF_Unknown && !(mFilter & (1 << FREF_Unknown)))
					return;
				if (ref->type == FREF_IncludeDirective && !(mFilter & (1 << FREF_IncludeDirective)))
					return;
				if (ref->type == FREF_Definition && !(mFilter & (1 << FREF_Definition)))
					return;
				if (ref->type == FREF_DefinitionAssign && !(mFilter & (1 << FREF_DefinitionAssign)))
					return;
				if (ref->type == FREF_Reference && !(mFilter & (1 << FREF_Reference)))
					return;
				if (ref->type == FREF_ReferenceAssign && !(mFilter & (1 << FREF_ReferenceAssign)))
					return;
				if (ref->type == FREF_ScopeReference && !(mFilter & (1 << FREF_ScopeReference)))
					return;
				if (ref->type == FREF_ReferenceAutoVar && !(mFilter & (1 << FREF_ReferenceAutoVar)))
					return;
				if (ref->type == FREF_JsSameName && !(mFilter & (1 << FREF_JsSameName)))
					return;
				if (ref->type == FREF_Creation && !(mFilter & (1 << FREF_Creation)))
					return;
				if (ref->type == FREF_Creation_Auto && !(mFilter & (1 << FREF_Creation_Auto)) &&
				    !(mFilter & (1 << FREF_ReferenceAutoVar)))
					return;
			}
			else
			{
				// rename dlg #RenameDlgFilter - filtering Rename, Change Signature, Encapsulate Field and Convert
				// Instance dialogs
				if ((ref->type == FREF_Comment || ref->type == FREF_IncludeDirective) &&
				    !(mFilter & (1 << FREF_Comment)))
					return;
			}

			if (ref->overridden && !(mFilter & (1 << FREF_Last)))
				return;

			// only check for file node if this reference will actually be displayed
			if (!mCurrentFile.IsEmpty())
				CreateFileNode();

			_ASSERTE(mTreeItemForCurrentFile);
			TreeReferenceInfo info = {ref->lnText.Wide(), mIconType, mTreeItemForCurrentFile, refId, type, ref};
#define OVERRIDE_IMAGE_OFFSET 5 // see GetTypeImgIdx() V_PRIVATE image + 1,
			if (ref->overridden)
			{
				if (type == FREF_Reference || type == FREF_ScopeReference)
					info.mIconType = ICONIDX_REFERENCE_OVERRIDDEN;
				else if (type == FREF_ReferenceAssign)
					info.mIconType = ICONIDX_REFERENCEASSIGN_OVERRIDDEN;
				else
					info.mIconType += OVERRIDE_IMAGE_OFFSET;
			}
			else if (type == FREF_Unknown || type == FREF_IncludeDirective)
				info.mIconType = ICONIDX_SUGGESTION;
			else if (type == FREF_Comment)
				info.mIconType = ICONIDX_COMMENT_BULLET;
			else if (type == FREF_Reference || type == FREF_ReferenceAutoVar || type == FREF_ScopeReference)
				info.mIconType = ICONIDX_REFERENCE;
			else if (type == FREF_ReferenceAssign)
				info.mIconType = ICONIDX_REFERENCEASSIGN;
			else if (type == FREF_Creation || type == FREF_Creation_Auto)
				info.mIconType = ICONIDX_REFERENCE_CREATION;

			// add on UI thread so that tree hash flags don't get corrupted
			if(g_mainThread == ::GetCurrentThreadId())
				mReferencesWnd->OnAddReference((WPARAM)&info, NULL);
			else
				mReferencesWnd->SendMessage(WM_ADD_REFERENCE, (WPARAM)&info, NULL);
			mHiddenReferences--;
		}
	}

	virtual void OnFoundInProject(const CStringW& project, int refID)
	{
//		RemoveProjectNodeIfEmpty();
		mCurrentProject = project;
		mCurrentProjectRefId = refID;
//		CreateProjectNode();
		mCurrentFile.Empty();
		mCurrentFileRefId = 0;
	}

	virtual void OnFoundInFile(const CStringW& file, int refID)
	{
		mCurrentFile = file;
		mCurrentFileRefId = refID;
	}

	virtual void OnSetProgress(int i)
	{
		if (mCanceled)
			return;

		// Note: this is a synchronous call from find refs thread to UI thread [case: 43506]
//		mReferencesWnd->SendMessage(WM_FIND_PROGRESS, (WPARAM)i, 0);
		mReferencesWnd->SetSearchProgressFromAnyThread(i);
	}

	virtual void OnPreSearch()
	{
		mFileCount = 0;
		mCurrentFile.Empty();
		mCurrentProject.Empty();
		mReferencesWnd->OnSearchBegin();
	}

	virtual void OnPostSearch()
	{
		if (!IsWindow(mReferencesWnd->GetSafeHwnd()))
			return;

//		RemoveProjectNodeIfEmpty();
		mReferencesWnd->SendMessage(WM_HIDDEN_REFS, (WPARAM)mHiddenReferences, 0);
#if defined(RAD_STUDIO)
		// we need to start new thread, so post to allow end this thread
		mReferencesWnd->PostMessage(WM_FIND_COMPLETE, (WPARAM)mFileCount, (LPARAM)mCanceled);
#else
		mReferencesWnd->SendMessage(WM_FIND_COMPLETE, (WPARAM)mFileCount, (LPARAM)mCanceled);
#endif
	}

  private:
	void CreateProjectNode()
	{
		if (mCurrentProject.IsEmpty())
			return;

		CStringW textForImage(mCurrentProject);
		int pos = textForImage.Find(L" (deferred)");
		if (-1 != pos)
			textForImage = textForImage.Left(pos);
		int img = GetFileImgIdx(textForImage);
		if (ICONIDX_FILE == img && gShellAttr->IsDevenv11OrHigher())
			img = ICONIDX_SCOPEPROJECT;
		TreeReferenceInfo info = {mCurrentProject, img, NULL, int(kFileRefBit | mCurrentProjectRefId), 0, NULL};
		if (g_mainThread == ::GetCurrentThreadId())
			mReferencesWnd->OnAddProjectGroup((WPARAM)&info, NULL);
		else
			mReferencesWnd->SendMessage(WM_ADD_PROJECTGROUP, (WPARAM)&info, NULL);
		mTreeItemForCurrentProject = info.mParentTreeItem;
		_ASSERTE(mTreeItemForCurrentProject);
		mCurrentProject.Empty();
//		mCurrentFile.Empty();
	}

	void CreateFileNode()
	{
		if (!mCurrentProject.IsEmpty())
			CreateProjectNode();

		int img = GetFileImgIdx(mCurrentFile);
		TreeReferenceInfo info = {
		    mCurrentFile, img, mTreeItemForCurrentProject, int(kFileRefBit | mCurrentFileRefId), 0, NULL};
		if (g_mainThread == ::GetCurrentThreadId())
			mReferencesWnd->OnAddFileReference((WPARAM)&info, NULL);
		else
			mReferencesWnd->SendMessage(WM_ADD_FILEREFERENCE, (WPARAM)&info, NULL);
		mTreeItemForCurrentFile = info.mParentTreeItem;
		_ASSERTE(mTreeItemForCurrentFile);
		mFileCount++;
		mCurrentFile.Empty();
	}

// 	void RemoveProjectNodeIfEmpty()
// 	{
// 		if (!mTreeItemForCurrentProject || mTreeItemForCurrentProject == TVI_ROOT)
// 			return;
// 
// 		if (mReferencesTree->ItemHasChildren(mTreeItemForCurrentProject))
// 			return;
// 
// 		// if mTreeItemForCurrentProject has no children, then remove and clear it
// 		mReferencesTree->DeleteItem(mTreeItemForCurrentProject);
// 		mTreeItemForCurrentProject = TVI_ROOT;
// 	}
};

ReferencesWndBase::ReferencesWndBase(const char* settingsCategory, UINT idd, CWnd* pParent, bool displayProjectNodes,
                                     Freedom fd, UINT nFlags)
    : CThemedVADlg(idd, pParent, fd, nFlags), m_treeSubClass(*this), m_tree(m_treeSubClass.GetTreeCtrl()),
      mRefs(std::make_shared<FindReferences>(ICONIDX_SUGGESTION)), mSettingsCategory(settingsCategory),
      mDisplayProjectNodes(displayProjectNodes), mHonorsDisplayTypeFilter(false), mDisplayTypeFilter(0xffffffff),
      mSharedFileBehavior(FindReferencesThread::sfOnce)
{
	mRefs->SetSharedThis(mRefs);
	ZeroMemory(mRefTypes, sizeof(mRefTypes));
}

ReferencesWndBase::ReferencesWndBase(const FindReferences& refsToCopy, const char* settingsCategory, UINT idd,
                                     CWnd* pParent, bool displayProjectNodes, Freedom fd /*= fdAll*/,
                                     UINT nFlags /*= flDefault*/, DWORD filter /*= 0xffffffff*/)
    : CThemedVADlg(idd, pParent, fd, nFlags), m_treeSubClass(*this), m_tree(m_treeSubClass.GetTreeCtrl()),
      mRefs(std::make_shared<FindReferences>(refsToCopy)), mSettingsCategory(settingsCategory),
      mDisplayProjectNodes(displayProjectNodes), mHonorsDisplayTypeFilter(true), mDisplayTypeFilter(filter),
      mSharedFileBehavior(FindReferencesThread::sfOnce)
{
	mRefs->SetSharedThis(mRefs);
	ZeroMemory(mRefTypes, sizeof(mRefTypes));
}

ReferencesWndBase::~ReferencesWndBase()
{
	ClearThread();
	if (g_References == mRefs)
		g_References = nullptr;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(ReferencesWndBase, CThemedVADlg)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
//{{AFX_MSG_MAP(FindUsageDlg)
ON_NOTIFY(NM_RCLICK, IDC_TREE1, OnRightClickTree)
//}}AFX_MSG_MAP
ON_REGISTERED_MESSAGE(WM_SHOW_PROGRESS, OnShowProgress)
ON_REGISTERED_MESSAGE(WM_ADD_REFERENCE, OnAddReference)
ON_REGISTERED_MESSAGE(WM_ADD_FILEREFERENCE, OnAddFileReference)
ON_REGISTERED_MESSAGE(WM_ADD_PROJECTGROUP, OnAddProjectGroup)
ON_REGISTERED_MESSAGE(WM_FIND_COMPLETE, OnSearchCompleteMsg)
ON_REGISTERED_MESSAGE(WM_FIND_PROGRESS, OnSearchProgress)
ON_REGISTERED_MESSAGE(WM_HIDDEN_REFS, OnHiddenRefCount)
END_MESSAGE_MAP()
#pragma warning(pop)

BOOL ReferencesWndBase::OnInitDialog(BOOL doProgressRepos)
{
	__super::OnInitDialog();

	// cannot subclass any more because window contains children
	CRect rect;
	HWND hTree = ::GetDlgItem(m_hWnd, IDC_TREE1);
	if (hTree)
	{
		::GetWindowRect(hTree, &rect);
		::SendMessage(hTree, WM_CLOSE, 0, 0);
		::SendMessage(hTree, WM_DESTROY, 0, 0);
	}
	else
	{
		_ASSERTE(hTree);
	}
	ScreenToClient(rect);

	for (int cnt = 0; cnt < 10; ++cnt)
	{
		if (m_treeSubClass.CreateEx(0, NULL, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP, rect, this, IDC_TREE1))
			break;

		// add retry for [case: 111864]
		vLog("WARN: ReferencesWndBase::OnInitDialog call to CreateEx failed\n");
		Sleep(100u + (cnt * 50u));
	}

	const DWORD style = TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_DISABLEDRAGDROP | TVS_SHOWSELALWAYS;
	if (m_tree.IsExtendedThemeActive())
		m_tree.ModifyStyle(0, style | TVS_FULLROWSELECT);
	else if (m_tree.IsVS2010ColouringActive())
		m_tree.ModifyStyle(TVS_FULLROWSELECT, style);
	else
		m_tree.ModifyStyle(0, style | TVS_FULLROWSELECT);
	gImgListMgr->SetImgListForDPI(m_tree, ImageListManager::bgTree, TVSIL_NORMAL);
	m_tree.SetFontType(VaFontType::EnvironmentFont);

	if (gShellAttr->IsMsdev())
		m_treeSubClass.ModifyStyleEx(0, WS_EX_STATICEDGE);
	else if (!m_tree.IsExtendedThemeActive())
		m_treeSubClass.ModifyStyle(0, WS_BORDER);

	m_treeSubClass.chars_cache = std::make_unique<CalcItemWidthCharsCache>(m_tree.m_hWnd);

	{
		CRedirectRegistryToVA rreg;

		// setup columns
		HDITEM hditem;
		hditem.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
		hditem.fmt = HDF_LEFT | HDF_STRING;
		for (int i = 0; i < __column_countof; i++)
		{
#ifdef ALLOW_MULTIPLE_COLUMNS
			hditem.cxy = AfxGetApp()->GetProfileInt(mSettingsCategory, format("column%ld_width", i).c_str(),
			                                        column_definitions[i].default_width);
#else
			hditem.cxy = 200;
#endif
			hditem.pszText = (char*)column_definitions[i].name;
			m_treeSubClass.GetHeaderCtrl().InsertItem(i, &hditem);
			m_treeSubClass.UpdateColumns();
#ifdef ALLOW_MULTIPLE_COLUMNS
			m_treeSubClass.ShowColumn(i, !AfxGetApp()->GetProfileInt(mSettingsCategory,
			                                                         format("column%ld_hidden", i).c_str(),
			                                                         column_definitions[i].default_hidden));
#endif
		}
		m_treeSubClass.UpdateColumns();
	}

	if (doProgressRepos)
	{
		CRect prect;
		GetDlgItem(IDC_PROGRESS1)->GetWindowRect(prect);
		ScreenToClient(prect);
		CRect srect;
		GetDlgItem(IDCANCEL)->GetWindowRect(srect);
		ScreenToClient(srect);
		prect.OffsetRect(srect.left - prect.right - 10, 0);
		GetDlgItem(IDC_PROGRESS1)->MoveWindow(prect);
	}

	AddSzControl(IDC_STATUS, mdResize, mdNone);
	AddSzControl(IDC_PROGRESS1, mdRepos, mdNone);
	RegisterReferencesControlMovers(); // it was AddSzControl(IDCANCEL, mdRepos, mdNone);
	AddSzControl(m_treeSubClass, mdResize, mdResize);

	if (Psettings && Psettings->mUseTooltipsInFindReferencesResults)
		m_tree.EnableToolTips();
	GetDlgItem(IDC_PROGRESS1)->ShowWindow(SW_HIDE);

	mProgressBar = (CProgressCtrl*)GetDlgItem(IDC_PROGRESS1);
	_ASSERTE(mProgressBar);
	mProgressBar->SetRange(0, 100);

	return FALSE; // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void ReferencesWndBase::ClearThread()
{
	bool doDelete = true;
	if (mFindRefsThread)
	{
		mFindRefsThread->Cancel(TRUE);
		if (mFindRefsThread->IsRunning())
		{
			mFindRefsThread->Wait(500);
			if (mFindRefsThread->IsRunning())
			{
				// postpone thread deletion or else we risk deadlock if the thread
				// is in the middle of doing a SendMessage while the current thread
				// is waiting for that thread
				doDelete = false;
			}
		}
	}

	if (doDelete)
		delete mFindRefsThread;
	else
	{
		AutoLockCs l(gUndeletedFindThreadsLock);
		gUndeletedFindThreads.push_back(mFindRefsThread);
	}
	mFindRefsThread = NULL;
}

void ReferencesWndBase::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(FindUsageDlg)
	//}}AFX_DATA_MAP
}

void ReferencesWndBase::PopulateListFromRefs()
{
	ReferencesThreadObserver obs(this, mRefs->GetReferenceImageIdx(), mHonorsDisplayTypeFilter, GetFilter());

	obs.OnPreSearch();
	const int kCnt = (int)mRefs->Count();
	CStringW currentRefFile;
	CStringW currentRefProject;
	for (int idx = 0; idx < kCnt; ++idx)
	{
		FindReference* ref = mRefs->GetReference((uint)idx);
		if (!ref)
			continue;

		const CStringW curProject(ref->mProject);
		if (curProject != currentRefProject && !curProject.IsEmpty())
		{
			currentRefProject = curProject;
			currentRefFile.Empty();
			obs.OnFoundInProject(currentRefProject, idx);
		}

		const CStringW curFile(ref->file);
		if (curFile != currentRefFile && !curFile.IsEmpty())
		{
			obs.OnFoundInFile(curFile, idx);
			currentRefFile = curFile;
		}

		obs.OnReference(idx);
	}
	obs.OnPostSearch();
}

void ReferencesWndBase::OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0; // let the tree send us a WM_CONTEXTMENU msg

	const CPoint pt(GetCurrentMessage()->pt);
	TVHITTESTINFO ht = {{0}};
	ht.pt = pt;
	::MapWindowPoints(HWND_DESKTOP, m_tree.m_hWnd, &ht.pt, 1);

	if (m_tree.HitTest(&ht) && (ht.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT)))
	{
		if (ht.hItem)
			m_tree.SelectItem(ht.hItem);
	}
}

void ReferencesWndBase::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
	__super::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
	CMenuXP::SetXPLookNFeel(this, pPopupMenu->m_hMenu, CMenuXP::GetXPLookNFeel(this));
}

void ReferencesWndBase::OnMeasureItem(int, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	CMenuXP::OnMeasureItem(lpMeasureItemStruct);
}

void ReferencesWndBase::OnDrawItem(int id, LPDRAWITEMSTRUCT dis)
{
	auto dpiScope = VsUI::DpiHelper::SetDefaultForWindow(m_hWnd);

	if (CMenuXP::OnDrawItem(dis, m_hWnd))
		return;

	if (id != IDC_CLOSE)
	{
		__super::OnDrawItem(id, dis);
		return;
	}

	CRect rect;
	::GetClientRect(dis->hwndItem, rect);

	CPen pen;
	pen.CreatePen(PS_SOLID, VsUI::DpiHelper::LogicalToDeviceUnitsX(1), RGB(0, 0, 0));

	CDC* dc = CDC::FromHandle(dis->hDC);
	HGDIOBJ hOldPen = dc->SelectObject(pen);
	dc->SetBkMode(TRANSPARENT);
	dc->Rectangle(rect);

	if (dis->itemState & ODS_SELECTED)
		dc->OffsetWindowOrg(-VsUI::DpiHelper::LogicalToDeviceUnitsX(1), -VsUI::DpiHelper::LogicalToDeviceUnitsY(1));

	dc->MoveTo(VsUI::DpiHelper::LogicalToDeviceUnitsX(2), VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->LineTo(rect.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(2),
	           rect.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->MoveTo(rect.right - VsUI::DpiHelper::LogicalToDeviceUnitsX(3), VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->LineTo(VsUI::DpiHelper::LogicalToDeviceUnitsX(1), rect.bottom - VsUI::DpiHelper::LogicalToDeviceUnitsY(2));
	dc->SelectObject(hOldPen);
}

LRESULT
ReferencesWndBase::OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu)
{
	if (CMenuXP::IsOwnerDrawn(pMenu->m_hMenu))
	{
		return CMenuXP::OnMenuChar(pMenu->m_hMenu, nChar, nFlags);
	}
	return __super::OnMenuChar(nChar, nFlags, pMenu);
}

void ReferencesWndBase::OnSearchBegin()
{
	mReferencesCount = 0;
	mSelectionFixed = false;
	mLastRefAdded = NULL;
	UpdateStatus(FALSE, -1);

	mLastSearchPositionSent = -1;
	OnSearchProgress(0, 0);
	PostMessage(WM_SHOW_PROGRESS, true);

	if (m_treeSubClass.chars_cache)
		m_treeSubClass.chars_cache->start();	// will refresh font/dpi if needed
}

void ReferencesWndBase::OnSearchComplete(int fileCount, bool wasCanceled)
{
	EnsureLastItemVisible();
	PostMessage(WM_SHOW_PROGRESS, false);
	UpdateStatus(TRUE, fileCount);

	if (m_treeSubClass.chars_cache)
		m_treeSubClass.chars_cache->end();
}

LRESULT
ReferencesWndBase::OnSearchProgress(WPARAM prog, LPARAM)
{
	mProgressBar->SetPos((int)prog);
	mProgressBar->RedrawWindow(NULL, NULL, RDW_INVALIDATE /*| RDW_UPDATENOW | RDW_ERASE*/ | RDW_FRAME);
	return 1;
}
void ReferencesWndBase::SetSearchProgressFromAnyThread(int i)
{
	// don't update progress unless higher than before (or reset to -1)
	int last = mLastSearchPositionSent.load();
	do 
	{
		if(i != -1)
		{
			if (i <= last)
				return;
		}
		else
		{
			if (last == -1)
				return;
		}
	} while (!mLastSearchPositionSent.compare_exchange_weak(last, i));

#ifndef VA_CPPUNIT
	extern std::atomic_uint64_t rfmt_progress;
	++rfmt_progress;
#endif
	RunFromMainThread2([hwnd = m_hWnd, i] {
		if (::IsWindow(hwnd))
			::SendMessage(hwnd, WM_FIND_PROGRESS, (WPARAM)i, 0);
	});
}

LRESULT ReferencesWndBase::OnHiddenRefCount(WPARAM hiddenRefCount, LPARAM)
{
	mHiddenReferenceCount = (uint)hiddenRefCount;
	return 1;
}

LRESULT
ReferencesWndBase::OnSearchCompleteMsg(WPARAM fileCount, LPARAM canceled)
{
	OnSearchComplete((int)fileCount, !!canceled);
	return 1;
}

void ReferencesWndBase::FindCurrentSymbol(const WTString& symScope, int typeImgIdx)
{
	if (mRefs->flags & FREF_Flg_FindErrors)
	{
		WTString symscope("Parsing errors...");
		mRefs->Init(symscope, typeImgIdx);
		mFindRefsThread = new FindReferencesThread(
		    mRefs, new ReferencesThreadObserver(this, typeImgIdx, mHonorsDisplayTypeFilter, GetFilter()),
		    mSharedFileBehavior);
	}
	else
	{
		if (g_currentEdCnt)
		{
			g_currentEdCnt->m_lastScopePos = UINT_MAX;
			g_currentEdCnt->CurScopeWord();
		}
		WTString symscope = symScope;
		if (symscope.GetLength())
		{
			mRefs->Init(symscope, typeImgIdx);
			mFindRefsThread = new FindReferencesThread(
			    mRefs, new ReferencesThreadObserver(this, typeImgIdx, mHonorsDisplayTypeFilter, GetFilter()),
			    mSharedFileBehavior);
		}
		else
		{
			OnSearchComplete(0, true);
		}
	}
}

#if defined(RAD_STUDIO)
bool ReferencesWndBase::RAD_FindNextSymbol(const WTString& symScope, int typeImgIdx, unsigned int delay)
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
	{
		mFindRefsThread->Cancel();
		mFindRefsThread->Wait(1000, false);

		if (mFindRefsThread->IsRunning())
			delay = 500;
	}

	auto func = [&, this]() {
		ClearThread();

		mRefs->Init(symScope, typeImgIdx, false);

		mFindRefsThread = new FindReferencesThread(
		    mRefs, new ReferencesThreadObserver(this, typeImgIdx, mHonorsDisplayTypeFilter, GetFilter()),
		    mSharedFileBehavior);

		mFindRefsThread->Wait(1000);
	};

	if (delay)
		DelayedInvoke(delay, func);
	else
		func();

	return true;
}
#endif

void ReferencesWndBase::OnRefresh()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return; // don't refresh while thread is still running

	ClearThread();
	RemoveAllItems();
	mRefs->Init(mRefs->GetFindScope(), mRefs->GetReferenceImageIdx());
	CWnd* tmp = GetDlgItem(IDCANCEL);
	if (tmp)
	{
		tmp->EnableWindow(true);
		tmp->ShowWindow(SW_SHOWNA);
	}
	m_tree.SetFocus();
	mFindRefsThread = new FindReferencesThread(
	    mRefs, new ReferencesThreadObserver(this, mRefs->GetReferenceImageIdx(), mHonorsDisplayTypeFilter, GetFilter()),
	    mSharedFileBehavior);
	mFindRefsThread->Wait(1000);
}

void ReferencesWndBase::GoToSelectedItemCB(LPVOID lpParam)
{
	ReferencesWndBase* _this = (ReferencesWndBase*)lpParam;
	_this->GoToSelectedItem();
}

void ReferencesWndBase::GoToSelectedItem()
{
	if (mIgnoreItemSelect)
		return;

	if (g_mainThread == ::GetCurrentThreadId())
	{
		new FunctionThread(GoToSelectedItemCB, this, "GoToSelectedItem", true);
		return;
	}

	HTREEITEM item = m_tree.GetSelectedItem();
	if (item)
		GoToItem(item);
}

LRESULT
ReferencesWndBase::OnShowProgress(WPARAM wparam, LPARAM /*lparam*/)
{
	GetDlgItem(IDC_PROGRESS1)->ShowWindow(wparam ? SW_SHOWNA : SW_HIDE);
	return 0;
}

void ReferencesWndBase::OnDoubleClickTree()
{
	GoToSelectedItem();
}

void ReferencesWndBase::RemoveAllItems()
{
	m_tree.SetRedraw(false);
	m_tree.DeleteAllItems();
	m_tree.SetRedraw(true);
	m_tree.RedrawWindow(nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

LRESULT
ReferencesWndBase::OnAddReference(WPARAM wparam, LPARAM /*lparam*/)
{
	const TreeReferenceInfo* info = reinterpret_cast<TreeReferenceInfo*>(wparam);
	if (info == nullptr)
		return 0;

	HTREEITEM insertAfter = TVI_LAST;

#if defined (RAD_STUDIO)
	// in second pass we are adding new items to the list, 
	// so we need to use insertion sort in order to have 
	// renaming happening properly from bottom to top

	if (info && info->mRef && info->mRef->RAD_pass == 2)
	{
		insertAfter = TVI_FIRST;

		HTREEITEM child = m_tree.GetChildItem(info->mParentTreeItem);
		while (child)
		{
			auto refId = (uint)m_tree.GetItemData(child);
			if (IS_FILE_REF_ITEM(refId))
				break;

			if (refId & kRADDesignerRefBit)
			{
				child = m_tree.GetNextSiblingItem(child);
				continue;
			}

			auto* fref = mRefs->GetReference(refId);
			if (!fref)
				continue;

			_ASSERTE(fref->fileId == info->mRef->fileId);			

			if (fref->fileId == info->mRef->fileId &&
				fref->pos < info->mRef->pos)
			{
				insertAfter = child;
				child = m_tree.GetNextSiblingItem(child);
				continue;
			}
	
			break;
		}
	}
#endif

// goal: setup treeitem in a single call
	uint nMask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
	int nImage = info->mIconType;
	int nSelectedImage = info->mIconType;
	uint nState = 0;
	uint nStateMask = 0;
	bool set_parent_state = false;

	if (info->mType == FREF_Unknown || info->mType == FREF_IncludeDirective)
	{
		nImage = nSelectedImage = ICONIDX_SUGGESTION;
		set_parent_state = true;
	}
	else if (info->mType == FREF_Comment)
	{
		nImage = nSelectedImage = ICONIDX_COMMENT_BULLET;
		nStateMask = TVIS_STATEIMAGEMASK;
		nState = INDEXTOSTATEIMAGEMASK((Psettings->mRenameCommentAndStringReferences ? 2u : 1u));
		nMask |= TVIF_STATE;
	}
	else
	{
		// ref icons are set in ReferencesThreadObserver::OnReference()
		if (!info->mRef || !info->mRef->overridden || Psettings->mRenameWiderScopeReferences)
		{
			nStateMask = TVIS_STATEIMAGEMASK;
			nState = INDEXTOSTATEIMAGEMASK((true ? 2u : 1u));
			nMask |= TVIF_STATE;
		}
	}

	LPARAM lParam = (uint)info->mRefId;
	HTREEITEM it = m_tree.InsertItemW(nMask, info->mText, nImage, nSelectedImage, nState, nStateMask, lParam, info->mParentTreeItem, insertAfter);

	mReferencesCount++;
	int depth = 0;
	if(info->mParentTreeItem && (info->mParentTreeItem != TVI_ROOT))
		depth = 1 + (current_file_is_in_a_project_group ? 1 : 0);
	m_treeSubClass.SetItemFlags2(it, TIF_PROCESS_MARKERS, 0, 
								 &info->mParentTreeItem, &depth, &info->mText);
	if(set_parent_state)
		m_tree.SetItemState(info->mParentTreeItem, TVIS_CUT, TVIS_CUT);


	if (m_tree.GetSelectedItem())
		mLastRefAdded = NULL;
	else
		mLastRefAdded = it;

#ifndef ALLOW_MULTIPLE_COLUMNS
	InterlockedIncrement(&m_treeSubClass.reposition_controls_pending);
#endif
	return 1;
}

LRESULT
ReferencesWndBase::OnAddFileReference(WPARAM wparam, LPARAM /*lparam*/)
{
//	EnsureLastItemVisible();
	TreeReferenceInfo* info = reinterpret_cast<TreeReferenceInfo*>(wparam);
	_ASSERTE(!info->mType);
	// passed in info->mParentTreeItem might be a project node or might be TVI_ROOT
	CStringW itemText(info->mText);
	int pos = itemText.ReverseFind('\\');
	if (-1 != pos++)
	{
		CStringW base(itemText.Left(pos));
		base += MARKER_BOLD;
		base += itemText.Mid(pos);
		base += MARKER_NONE;
		itemText = base;
	}
	auto item = m_tree.InsertItemW(
		TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_PARAM, 
		itemText, 
		info->mIconType, info->mIconType, 
		TVIS_EXPANDED | INDEXTOSTATEIMAGEMASK(true ? 2 : 1), TVIS_EXPANDED | TVIS_STATEIMAGEMASK, 
		(uint)info->mRefId, info->mParentTreeItem, TVI_LAST
	);
	current_file_is_in_a_project_group = info->mParentTreeItem && (info->mParentTreeItem != TVI_ROOT);
	int depth = current_file_is_in_a_project_group ? 1 : 0;
	m_treeSubClass.SetItemFlags2(item, 
								 TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR | TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS, 0, 
								 &info->mParentTreeItem, &depth, &itemText);
	info->mParentTreeItem = item;

	if (!mSelectionFixed)
	{
		mSelectionFixed = true;
		m_tree.SelectItem(NULL);
	}

#ifndef ALLOW_MULTIPLE_COLUMNS
	// reset on new file
	InterlockedExchange(&m_treeSubClass.reposition_controls_pending, 1);
#endif
	return 1;
}

LRESULT
ReferencesWndBase::OnAddProjectGroup(WPARAM wparam, LPARAM lparam)
{
	EnsureLastItemVisible();
	TreeReferenceInfo* info = reinterpret_cast<TreeReferenceInfo*>(wparam);
	_ASSERTE(!info->mType);
	_ASSERTE(!info->mParentTreeItem);
	if (!mDisplayProjectNodes)
	{
		info->mParentTreeItem = TVI_ROOT;
		return 1;
	}

	CStringW itemText(info->mText);
	int pos = itemText.ReverseFind('\\');
	if (-1 != pos++)
	{
		CStringW base(itemText.Left(pos));
		base += MARKER_BOLD;
		base += itemText.Mid(pos);
		base += MARKER_NONE;
		itemText = base;
	}
	info->mParentTreeItem = m_tree.InsertItemW(
		TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_PARAM, 
		itemText, 
		info->mIconType, info->mIconType, 
		TVIS_EXPANDED | INDEXTOSTATEIMAGEMASK(true ? 2 : 1), TVIS_EXPANDED | TVIS_STATEIMAGEMASK, 
		(uint)info->mRefId, TVI_ROOT, TVI_LAST
	);
	int depth = 0;
	HTREEITEM parentitem = nullptr;
	m_treeSubClass.SetItemFlags2(info->mParentTreeItem, TIF_ONE_CELL_ROW | TIF_PROCESS_MARKERS | TIF_DONT_COLOUR | TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS, 0, &parentitem, &depth, &itemText);

#ifndef ALLOW_MULTIPLE_COLUMNS
	// reset on project
	InterlockedExchange(&m_treeSubClass.reposition_controls_pending, 1);
#endif
	return 1;
}

void ReferencesWndBase::OnToggleFilterIncludes()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	Psettings->mFindRefsDisplayIncludes = !Psettings->mFindRefsDisplayIncludes;
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterUnknowns()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	Psettings->mFindRefsDisplayUnknown = !Psettings->mFindRefsDisplayUnknown;
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterInherited()
{
	Psettings->mDisplayWiderScopeReferences = !Psettings->mDisplayWiderScopeReferences;
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterComments()
{
	Psettings->mDisplayCommentAndStringReferences = !Psettings->mDisplayCommentAndStringReferences;
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterDefinitions()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	// filter ui does not differentiate between FREF_Definition and FREF_DefinitionAssign
	mDisplayTypeFilter ^= (1 << FREF_DefinitionAssign);
	mDisplayTypeFilter ^= (1 << FREF_Definition);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterDefinitionAssigns()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	// filter ui does not differentiate between FREF_Definition and FREF_DefinitionAssign
	mDisplayTypeFilter ^= (1 << FREF_DefinitionAssign);
	mDisplayTypeFilter ^= (1 << FREF_Definition);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterReferences()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	mDisplayTypeFilter ^= (1 << FREF_Reference);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterReferenceAssigns()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	mDisplayTypeFilter ^= (1 << FREF_ReferenceAssign);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterScopeReferences()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	mDisplayTypeFilter ^= (1 << FREF_ScopeReference);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterJsSameNames()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	mDisplayTypeFilter ^= (1 << FREF_JsSameName);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterAutoVars()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	mDisplayTypeFilter ^= (1 << FREF_ReferenceAutoVar);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::OnToggleFilterCreations()
{
	_ASSERTE(mHonorsDisplayTypeFilter);
	mDisplayTypeFilter ^= (1 << FREF_Creation);
	mDisplayTypeFilter ^= (1 << FREF_Creation_Auto);
	RemoveAllItems();
	PopulateListFromRefs();
}

void ReferencesWndBase::EnsureLastItemVisible()
{
	if (!mLastRefAdded)
		return;

	if (!m_tree.GetSelectedItem())
	{
		// when there is no selection, this causes the results list to scroll.
		m_tree.EnsureVisible(mLastRefAdded);
	}
	mLastRefAdded = NULL;
}

void ReferencesWndBase::GoToItem(HTREEITEM item)
{
	const int i = (int)m_tree.GetItemData(item);
	const uint refId = i & ~kFileRefBit;
	if (IS_FILE_REF_ITEM(i))
	{
		const FindReference* ref = mRefs->GetReference(refId);
		if (!ref)
			return;

		const CString itemTxt(m_tree.GetItemText(item));
		if (itemTxt == CString(ref->mProject))
			return; // no goto for the project node
	}

	mRefs->GotoReference((int)refId, IS_FILE_REF_ITEM(i));
}

void ReferencesWndBase::OnCancel()
{
	HIMAGELIST lst = (HIMAGELIST)m_tree.SendMessage(TVM_GETIMAGELIST, TVSIL_STATE, 0);
	if (lst)
		ImageList_Destroy(lst);

	__super::OnCancel();
}

void ReferencesWndBase::OnOK()
{
	HIMAGELIST lst = (HIMAGELIST)m_tree.SendMessage(TVM_GETIMAGELIST, TVSIL_STATE, 0);
	if (lst)
		ImageList_Destroy(lst);

	__super::OnOK();
}

void ReferencesWndBase::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	m_tree.PopTooltip();
	HTREEITEM selItem = m_tree.GetSelectedItem();
	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && selItem)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (m_tree.GetItemRect(selItem, &rc, TRUE))
		{
			m_tree.ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}

	WTString txt;
	CMenu contextMenu;
	contextMenu.CreatePopupMenu();

	OnPopulateContextMenu(contextMenu);

	CMenuXP::SetXPLookNFeel(this, contextMenu);

	MenuXpHook hk(this);
	contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
}

void ReferencesWndBase::InspectContents(HTREEITEM item)
{
	if (TVI_ROOT == item)
	{
		mHasOverrides = false;
		ZeroMemory(mRefTypes, sizeof(mRefTypes));
	}

	HTREEITEM childItem = m_tree.GetChildItem(item);
	while (childItem)
	{
		int data = (int)m_tree.GetItemData(childItem);
		if (!IS_FILE_REF_ITEM(data))
		{
			FindReference* ref = mRefs->GetReference((uint)data);
			if (ref)
			{
				if (ref->overridden)
					mHasOverrides = true;

				_ASSERTE(ref->type < FREF_Last);
				mRefTypes[ref->type]++;
			}
		}

		if (m_tree.ItemHasChildren(childItem))
			InspectContents(childItem);

		childItem = m_tree.GetNextSiblingItem(childItem);
	}
}

void ReferencesWndBase::InspectReferences()
{
	mHasOverrides = false;
	ZeroMemory(mRefTypes, sizeof(mRefTypes));
	const size_t kCnt = mRefs->Count();
	for (size_t idx = 0; idx < kCnt; ++idx)
	{
		FindReference* ref = mRefs->GetReference(idx);
		if (!ref)
			continue;

		if (ref->overridden)
			mHasOverrides = true;

		_ASSERTE(ref->type < FREF_Last);
		mRefTypes[ref->type]++;
	}
}

void ReferencesWndBase::SetSharedFileBehavior(FindReferencesThread::SharedFileBehavior b)
{
	// [case: 80212]
	if (Psettings->mFindRefsAlternateSharedFileBehavior)
		mSharedFileBehavior = b;
	else
		mSharedFileBehavior = FindReferencesThread::sfOnce;
}

DWORD
ReferencesWndBase::GetFilter()
{
	if (mHonorsDisplayTypeFilter)
	{
		mDisplayTypeFilter =
		    (mDisplayTypeFilter & (1 << FREF_Definition)) | (mDisplayTypeFilter & (1 << FREF_DefinitionAssign)) |
		    (mDisplayTypeFilter & (1 << FREF_Reference)) | (mDisplayTypeFilter & (1 << FREF_ReferenceAssign)) |
		    (mDisplayTypeFilter & (1 << FREF_ScopeReference)) | Psettings->mFindRefsDisplayUnknown << FREF_Unknown |
		    GetCommentState() << FREF_Comment | (mDisplayTypeFilter & (1 << FREF_JsSameName)) |
		    (mDisplayTypeFilter & (1 << FREF_ReferenceAutoVar)) |
		    Psettings->mFindRefsDisplayIncludes << FREF_IncludeDirective | (mDisplayTypeFilter & (1 << FREF_Creation)) |
		    (mDisplayTypeFilter & (1 << FREF_Creation_Auto)) | Psettings->mDisplayWiderScopeReferences << FREF_Last;
	}
	else
	{
		mDisplayTypeFilter = 1u << FREF_Definition | 1u << FREF_DefinitionAssign | 1u << FREF_Reference |
		                     1u << FREF_ReferenceAssign | 1u << FREF_ScopeReference |
		                     Psettings->mFindRefsDisplayUnknown << FREF_Unknown | GetCommentState() << FREF_Comment |
		                     1u << FREF_JsSameName | 1u << FREF_ReferenceAutoVar |
		                     Psettings->mFindRefsDisplayIncludes << FREF_IncludeDirective | 1u << FREF_Creation |
		                     1u << FREF_Creation_Auto | Psettings->mDisplayWiderScopeReferences << FREF_Last;
	}

	return mDisplayTypeFilter;
}

bool ReferencesWndBase::GetCommentState()
{
	return Psettings->mDisplayCommentAndStringReferences;
}

void ReferencesWndBase::RegisterReferencesControlMovers()
{
	AddSzControl(IDCANCEL, mdRepos, mdNone);
}
