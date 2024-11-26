// VAFileView.cpp : implementation file
//

#include "stdafxed.h"
#include "edcnt.h"
#include "expansion.h"
#include "Project.h"
#include "SymbolListCombo.h"
#include "FileListCombo.h"
#include "VAFileView.h"
#include "DevShellAttributes.h"
#include "MenuXP/Tools.h"
#include "file.h"
#include "wt_stdlib.h"
#include "Registry.h"
#include "RegKeys.h"
#include "Settings.h"
#include "VACompletionBox.h"
#include "FileTypes.h"
#include "VaService.h"
#include "Directories.h"
#include "TokenW.h"
#include "VA_MRUs.h"
#include "WindowUtils.h"
#include "VAAutomation.h"
#include "IdeSettings.h"
#include "ImageListManager.h"
#include "VAWorkspaceViews.h"
#include "FontSettings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::string;
using OWL::TRegexp;

#ifdef USE_NEW_MRU_TREE
#define ID_REFRESH 0x1233
#define IDC_INCLUDE_ALL_RECENT_FILES_BIT 0x4
#define IDC_EXPAND_TREE_BIT 0x10

class MRUEntry
{
  public:
	MRUEntry(const CStringW& file, CStringW scope, int type, int line, UINT attr)
	    : mFile(file), mScope(scope), mType(type), mScopeOffset(line), mAttr(attr)
	{
	}

	CStringW mFile;
	CStringW mScope;
	int mType;
	int mScopeOffset;
	int mAttr;
};

class MRUList
{
	CStringW mSaveToFile;

  public:
	typedef std::vector<MRUEntry*> MRUEntries;
	MRUEntries mEntries;
	MRUList()
	{
	}
	~MRUList()
	{
		SaveToFile();
		Clear();
	}
	void SaveToFile()
	{
		if (mSaveToFile.GetLength())
		{
			WTofstream ofs(mSaveToFile);
			CStringW ln;
			// Write out in reverse order so they get read in correctly
			for (uint i = mEntries.size(), count = 0; i && count < 100; i--, count++)
			{
				MRUEntry* item = mEntries[i - 1];
				CString__FormatW(ln, L"%s|%s|%d|%d|%d\r\n", item->mFile, item->mScope, item->mScopeOffset, item->mType,
				                 item->mAttr);
				ofs.Write((LPCVOID)(LPCWSTR)ln, ln.GetLength() * sizeof(WCHAR));
			}
		}
	}
	void ReadFromFile(const CStringW& file)
	{
		if (mSaveToFile.GetLength() && mEntries.size())
		{
			SaveToFile();
			Clear();
		}
		mSaveToFile = file;
		CStringW fileContents;
		ReadFileUtf16(mSaveToFile, fileContents);
		TokenW mru = fileContents;
		while (mru.more() > 2)
		{
			TokenW ln = mru.read(L"\r\n");
			CStringW file = ln.read(L"|");
			CStringW scope = ln.read(L"|");
			int line = _wtoi(ln.read(L"|"));
			int type = _wtoi(ln.read(L"|"));
			int attr = _wtoi(ln.read(L"|"));
			AddMRU(file, scope, type, line, attr);
		}
	}
	void AddMRU(const CStringW& file, CStringW scope, int type, int line, BOOL modified)
	{
		if (!(modified || Psettings->m_nMRUOptions & IDC_INCLUDE_ALL_RECENT_FILES_BIT))
			return;
		try
		{
			int count = mEntries.size();
			MRUEntry* lstItem = count ? mEntries[0] : NULL;
			if (count && mEntries[0]->mFile == file && mEntries[0]->mScope == scope)
			{
				if (modified && mEntries[0]->mAttr != modified) // save modified state
				{
					mEntries[0]->mAttr = modified;
					UpdateView();
				}
				mEntries[0]->mScopeOffset = line;
				return;
			}
			MRUEntry* item = new MRUEntry(file, scope, type, line, modified);
			mEntries.insert(mEntries.begin(), item);
			uint topPos = 1;

			// delete other entries with same file/scope
			for (uint i = 1; i < mEntries.size(); i++)
			{
				MRUEntry* lstItem = mEntries[i];
				if (mEntries[i]->mFile == file)
				{
					if (mEntries[i]->mScope == scope /*|| mEntries[i]->mLine == line*/)
					{
						if (mEntries[i]->mAttr && mEntries[i]->mScope == scope) // save modified state
							item->mAttr = mEntries[i]->mAttr;
						removeItem(i);
						i--;
					}
					else if (i > topPos)
					{
						// move under current file
						mEntries.insert(mEntries.begin() + (topPos), lstItem);
						mEntries.erase(mEntries.begin() + i + 1);
						topPos++;
					}
					else
						topPos = i + 1; // so the next goes adter this one
				}
			}
			// limit size of list
#define MAXSZ 100
			while (mEntries.size() > MAXSZ)
				removeItem(MAXSZ);
			if (modified || Psettings->m_nMRUOptions & IDC_INCLUDE_ALL_RECENT_FILES_BIT)
				UpdateView();
		}
		catch (...)
		{
			ASSERT(FALSE);
		}
#ifdef _DEBUG
		ValidateList();
#endif // _DEBUG
	}
	void removeItem(INT i)
	{
		delete mEntries[i];
		mEntries.erase(mEntries.begin() + i);
	}
	void Clear(uint id = -1)
	{
		if (id == -1)
		{
			while (mEntries.size())
				removeItem(0);
		}
		else if (id & 0xffff0000)
		{
			// remove child nodes
			id &= 0x0000ffff;
			CStringW file = mEntries[id]->mFile;
			while (mEntries.size() > id && mEntries[id]->mFile == file)
				removeItem(id);
		}
		else
			removeItem(id);
		UpdateView();
	}
	void GotoItem(int i)
	{
		CStringW file = mEntries[i & 0xffff]->mFile;
		int line = mEntries[i & 0xffff]->mScopeOffset;
		DelayFileOpen(file, line, nullptr);
	}
	virtual void UpdateView()
	{
	}

#ifdef _DEBUG
	CStringW contents;
	void ValidateList()
	{
		try
		{
			contents.Empty();
			for (UINT i = 0; i < mEntries.size(); i++)
				contents += mEntries[i]->mScope + L", ";
			contents.GetLength();
		}
		catch (...)
		{
			ASSERT(FALSE);
		}
	}
#endif // _DEBUG
};
class MRUListView : public MRUList
{
  public:
	VAFileView* mView;
	BOOL mExpand;
	MRUListView() : mView(NULL)
	{
		mExpand = FALSE; // Save to reg?
	}
	~MRUListView()
	{
		mView = NULL;
	}
	virtual void UpdateView()
	{
		if (mView && mView->IsWindow())
			mView->PostMessage(WM_COMMAND, ID_REFRESH, 0);
	}
	BOOL OnNotify(NMHDR* pNMHDR)
	{
		if (pNMHDR->code == TVN_ITEMEXPANDING)
		{
			NMTREEVIEW* pTREEHDR = (NMTREEVIEW*)pNMHDR;
			mExpand = (pTREEHDR->action == TVE_EXPAND);
		}
		return FALSE;
	}
	void UpdateTree()
	{
		CTreeCtrl* pList = (CTreeCtrl*)mView->GetDlgItem(IDC_TREE1);
		pList->SetRedraw(FALSE);
		pList->DeleteAllItems();
		int count = mEntries.size();
		CStringW curfile;
		HTREEITEM curnode = NULL;
#define MAX_ITEMS_PER_FILE 7
		int itemsInFile = 0;
		CStringW curEdFile;
		if (g_currentEdCnt)
			curEdFile = g_currentEdCnt->FileName();
		HTREEITEM curEdNode = NULL;
		for (int i = 0; i < count; i++)
		{
			MRUEntry* it = mEntries[i];
			if (!(Psettings->m_nMRUOptions & IDC_INCLUDE_ALL_RECENT_FILES_BIT) && !it->mAttr)
				continue;
			if (!curnode || curfile != it->mFile)
			{
				itemsInFile = 0;
				CStringW bname = Basename(it->mFile);
				EdCntPtr ed = GetOpenEditWnd(it->mFile);
				curnode = pList->InsertItem(WTString(bname), TVI_ROOT);
				int img = GetFileImgIdx(it->mFile);
				pList->SetItemImage(curnode, img, img);
				pList->SetItemData(curnode, 0x80000000 | i);
			}
			if ((itemsInFile++) > MAX_ITEMS_PER_FILE)
				continue;
			curfile = it->mFile;
			HTREEITEM item = pList->InsertItem(CleanScopeForDisplay(WTString(it->mScope)), curnode);
			int img = it->mType;
			pList->SetItemImage(item, img, img);
			pList->SetItemData(item, i);
			if (curfile == curEdFile)
			{
				pList->SetItemState(curnode, TVIS_BOLD, TVIS_BOLD);
				if (mExpand)
					pList->Expand(curnode, TVE_EXPAND);
				pList->EnsureVisible(curnode);
			}
		}
		pList->SetRedraw(TRUE);
		pList->UpdateWindow();
	}
};
static MRUListView sMRU;

#endif // USE_NEW_MRU_TREE

WTString g_MRU_Title;
int g_MRU_Type = 0;

void SetMRUItem(LPCSTR name, int type)
{
#ifdef USE_NEW_MRU_TREE
	g_MRU_Title = name;
	g_MRU_Type = type;
#endif // USE_NEW_MRU_TREE
}

void AddMRU(const CStringW& file, CStringW scope, int type, int line, BOOL modified)
{
#ifdef USE_NEW_MRU_TREE
	if (g_currentEdCnt)
	{
		if (!file.GetLength())
			sMRU.UpdateView();
		if (g_MRU_Type)
		{
			WTString sym = DBColonToSepStr(TokenGetField(g_MRU_Title, " ;()[]{}"));
			sMRU.AddMRU(g_currentEdCnt->FileName(), CStringW(sym), g_MRU_Type, g_currentEdCnt->CurLine(),
			            TRUE); // Go to Symbol
			g_MRU_Type = NULL;
		}
		else if (g_currentEdCnt->mruFlag)
			sMRU.AddMRU(file, scope, type, line, modified);
		static CStringW lfile;
		if (lfile != g_currentEdCnt->FileName())
		{
			sMRU.UpdateView();
			lfile = g_currentEdCnt->FileName();
		}

		g_currentEdCnt->mruFlag = NULL;
	}
#endif // USE_NEW_MRU_TREE
}

static const UINT_PTR kTimerId_ClearTooltip = 300u;

class VAFileView_TreeCtrl : public ToolTipWrapper<CColorVS2010TreeCtrl>
{
	std::unique_ptr<CGradientCache> background_gradient_cache;

	DECLARE_MESSAGE_MAP()

  public:
	VAFileView_TreeCtrl(HWND hParent, LPCSTR tipText, int yoffset = 10, BOOL ShouldDelete = TRUE)
	    : ToolTipWrapper<CColorVS2010TreeCtrl>(hParent, tipText, yoffset, ShouldDelete)
	{
	}

	afx_msg void OnTreeCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
	{
		if (IsVS2010VAViewColouringActive() && gPkgServiceProvider)
			TreeVS2010CustomDraw(*this, this, pNMHDR, pResult, background_gradient_cache);
		else
			*pResult = CDRF_DODEFAULT;
	}

	afx_msg BOOL OnEraseBkgnd(CDC* dc)
	{
		if (IsVS2010VAViewColouringActive() && gPkgServiceProvider)
			return 1;
		else
			return ToolTipWrapper<CColorVS2010TreeCtrl>::OnEraseBkgnd(dc);
	}

	afx_msg BOOL OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* result)
	{
		CPoint mouse((LPARAM)::GetMessagePos());
		ScreenToClient(&mouse);
		HTREEITEM itemUnderMouse = HitTest(mouse);
		if (itemUnderMouse != nullptr)
		{
			if (g_FontSettings)
				((CToolTipCtrl*)CWnd::FromHandle(pNMHDR->hwndFrom))
				    ->SetMaxTipWidth(g_FontSettings->m_tooltipWidth - 100);
			static wchar_t ttbuffer[4096];
			ttbuffer[0] = 0;
			TOOLTIPTEXTW* tttw = (TOOLTIPTEXTW*)pNMHDR;
			tttw->lpszText = ttbuffer;
			NMTVGETINFOTIPW git;
			memset(&git, 0, sizeof(git));
			git.hdr.code = TVN_GETINFOTIPW;
			git.hdr.hwndFrom = *this;
			git.hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
			git.hItem = itemUnderMouse;
			git.cchTextMax = sizeof(ttbuffer) / sizeof(ttbuffer[0]) - 1;
			git.pszText = ttbuffer;
			GetParent()->SendMessage(WM_NOTIFY, (WPARAM)GetDlgCtrlID(), (LPARAM)&git);
			if (result)
				*result = NULL;
		}
		return itemUnderMouse != nullptr ? TRUE : FALSE;
	}
};

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VAFileView_TreeCtrl, ToolTipWrapper<CColorVS2010TreeCtrl>)
ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnTreeCustomDraw)
ON_WM_ERASEBKGND()
ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// VAFileView dialog

VAFileView::VAFileView(CWnd* pParent /*=NULL*/)
    : VADialog((UINT)(gShellAttr->IsDevenv11OrHigher() ? IDD_FILEVIEWv11 : IDD_FILEVIEW), pParent, fdAll,
               uint(flAntiFlicker | flNoSavePos)),
      mTimerId(0)
{
	//{{AFX_DATA_INIT(VAFileView)
	//}}AFX_DATA_INIT
}

void VAFileView::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(VAFileView)
	//}}AFX_DATA_MAP
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VAFileView, VADialog)
//{{AFX_MSG_MAP(VAFileView)
ON_MENUXP_MESSAGES()
ON_WM_CREATE()
ON_WM_ERASEBKGND()
ON_WM_CONTEXTMENU()
ON_NOTIFY(NM_DBLCLK, IDC_TREE1, OnMruDoubleClick)
ON_NOTIFY(NM_RETURN, IDC_TREE1, OnMruDoubleClick)
ON_NOTIFY(NM_RCLICK, IDC_TREE1, OnMruRightClick)
ON_NOTIFY(NM_CLICK, IDC_TREE1, OnMruLeftClick)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

IMPLEMENT_MENUXP(VAFileView, VADialog);
/////////////////////////////////////////////////////////////////////////////
// VAFileView message handlers

static VAFileView* pThis = NULL;

int VAFileView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (__super::OnCreate(lpCreateStruct) == -1)
		return -1;
	pThis = this;
	return 0;
}

VAFileView::~VAFileView()
{
	if (::IsWindow(GetSafeHwnd()))
		LoadState("GOTO_MRU_DEST", TRUE);
	pThis = nullptr;
}

CStringW sLastProjectLoad;

void LoadProjectMRU(const CStringW& project)
{
	if (g_VA_MRUs)
		g_VA_MRUs->LoadProject(project);
#ifdef USE_NEW_MRU_TREE
	const CStringW MruDir = VaDirs::GetUserLocalDir() + L"MRU";
	CreateDir(MruDir);
	CStringW tfile = MruDir + L"\\mru_" + itosw(WTHashKeyW(project)) + L".txt";
	sMRU.ReadFromFile(tfile);
#else
	// [case: 77373]
	sLastProjectLoad = project;
	if (pThis->GetSafeHwnd())
		pThis->LoadState(WTString(sLastProjectLoad).c_str());
#endif // USE_NEW_MRU_TREE
}

static CStringW sLastMruFile;
static MruFileEntryReason sLastFileReason = mruFileEntryCount;

// Psettings->m_nMRUOptions bit values (or'd together):
// 		0x1 file edit (default value)
// 		0x2 file open (was never actually set anywhere; treat same as focus)
// 		0x4 file focus (if focus enabled, then edit is forced) (displayed in menu as "opened files")
// 		0x8 method edit

void VAProjectAddFileMRU(const CStringW& file, MruFileEntryReason reason)
{
	if (GlobalProject && !GlobalProject->IsOkToIterateFiles())
		return;

	if (g_VA_MRUs)
		g_VA_MRUs->m_FIS.AddToTop(file);

	int icon = ICONIDX_FIW;

	switch (reason)
	{
	case mruFileSelected:
		// not optional; item selected from FIS/OFIS
		break;
	case mruFileFocus:
		if (!(Psettings->m_nMRUOptions & 0x4))
			return;
		icon = ICONIDX_FILE;
		break;
	case mruFileOpen:
		// 		if (!(Psettings->m_nMRUOptions & 0x2))
		// 			return;
		if (!(Psettings->m_nMRUOptions & 0x4))
			return;
		icon = ICONIDX_SCOPECUR;
		break;
	case mruFileEdit:
		// focus is supposed to imply edit (per disable of edit when focus checked in mru menu)
		if (!(Psettings->m_nMRUOptions & 0x1 || Psettings->m_nMRUOptions & 0x4))
			return;
		icon = ICONIDX_MODFILE;
		break;
	default:
		_ASSERTE(!"unhandled mruFileEntryType");
		break;
	}

	if (!pThis)
		return;

	if (sLastMruFile != file)
	{
		pThis->Insert(Basename(file), icon, file, reason);
		sLastFileReason = reason;
		sLastMruFile = file;
	}
	else if (sLastFileReason != reason)
	{
		if (mruFileSelected != reason && reason < sLastFileReason) // should just change icon?
		{
			pThis->Insert(Basename(file), icon, file, 0);
			// insert causes reset, so restore previous values
			sLastFileReason = reason;
			sLastMruFile = file;
		}
	}
}

static WTString sLastMruScope;
void VAProjectAddScopeMRU(LPCSTR scope, MruMethodEntryReason reason)
{
	if (GlobalProject && !GlobalProject->IsOkToIterateFiles())
		return;

	int imgIdx = ICONIDX_SIW;
	switch (reason)
	{
	case mruMethodNav:
		break;
	case mruMethodEdit:
		if (!(Psettings->m_nMRUOptions & 0x8))
			return;
		imgIdx = ICONIDX_MODMETHOD;
		break;
	default:
		_ASSERTE(!"unhandled mruMethodEntryType");
		break;
	}

	if (!pThis)
		return;

	if (sLastMruScope == scope)
		return;

	token t = scope;
	t.ReplaceAll(TRegexp("<.*>"), string(""));
	// scope contains depth count - remove it
	t.ReplaceAll(TRegexp("-[0-9]+:"), string(":"));
	WTString fullscope;
	CStringW cls, meth;
	MultiParsePtr mp = MultiParse::Create(gTypingDevLang);
	for (int cnt = 0; t.more() > 1; ++cnt)
	{
		WTString s = t.read(DB_SEP_STR2);

#define EQ(str) (_tcsicmp(str, s.c_str()) == 0)
		if (EQ("foreach") || EQ("do") || EQ("try") || EQ("if") || EQ("for") || EQ("switch") || EQ("while") ||
		    EQ("BRC") || EQ("PP"))
		{
			break;
		}

		WTString fscope = fullscope + DB_SEP_STR + s;
		if (!mp->FindExact(fscope))
		{
			if (cnt)
				break;

			// [case: 56072] FindExact skips GOTODEF DTypes, try again
			DType* dt = mp->FindSym(&fscope, NULL, NULL, FDF_NoConcat | FDF_GotoDefIsOk);
			if (!dt || dt->type() != GOTODEF)
				break;
		}

		fullscope = fscope;
		cls = meth;
		meth = s.Wide();
	}

	if (cls.GetLength())
		meth = cls + L"." + meth;
	const CStringW file = sLastMruFile;
	const MruFileEntryReason previousReason = sLastFileReason;
	if (meth.GetLength())
		pThis->Insert(meth, imgIdx, fullscope.Wide());

	sLastFileReason = previousReason;
	sLastMruFile = file;
	sLastMruScope = scope;
}

CStringW GetItemTextW(CTreeCtrl* pList, HTREEITEM hItem)
{
	_ASSERTE(pList);
	TVITEMW item;
	item.hItem = hItem;
	item.mask = TVIF_TEXT;
	CStringW str;
	int nLen = 128;
	int nRes;
	do
	{
		nLen *= 2;
		item.pszText = str.GetBufferSetLength(nLen);
		item.cchTextMax = nLen;
		::SendMessageW(pList->m_hWnd, TVM_GETITEMW, 0, (LPARAM)&item);
		nRes = lstrlenW(item.pszText);
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}

void VAFileView::LoadState(LPCSTR key, BOOL save /* = FALSE */)
{
#if !defined(USE_NEW_MRU_TREE)
	WTString newKey = WTString("GOTO_MRU_2_") + itos((int)WTString(key).hash());
	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	if (m_mru_reg.IsEmpty() || m_mru_reg == newKey)
	{
		HTREEITEM item = pList->GetChildItem(TVI_ROOT);
		for (int i = 0; item; i++)
		{
			CStringW* file = (CStringW*)pList->GetItemData(item);
			delete file;
			item = pList->GetNextItem(item, TVGN_NEXT);
		}
	}
	else
	{
		// save state
		CStringW fileList;
		HTREEITEM item = pList->GetChildItem(TVI_ROOT);
		for (int i = 0; item; i++)
		{
			CStringW* file = (CStringW*)pList->GetItemData(item);
			if (i < 20)
			{
				const CStringW txt = ::GetItemTextW(pList, item);
				int img, sel;
				pList->GetItemImage(item, img, sel);
				if (file)
					fileList = txt + L'|' + ::itosw(img) + L'|' + *file + L';' + fileList;
				else
					fileList = txt + L'|' + ::itosw(img) + L';' + fileList;
			}
			if (file)
				delete file;
			item = pList->GetNextItem(item, TVGN_NEXT);
		}
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, m_mru_reg.c_str(), fileList);
	}
	pList->DeleteAllItems();

	// load new
	if (!save && key && *key)
	{
		TokenW fileList = GetRegValueW(HKEY_CURRENT_USER, ID_RK_APP, newKey.c_str());
		for (int i = 0; i < 40 && fileList.more();)
		{
			TokenW ln = fileList.read(L";");
			const CStringW filename = ln.read(L"|");
			int image = _wtoi(ln.read());
			if (filename.GetLength())
			{
				if (ln.more())
				{
					// If we load MRU from previous version, update the icons
					// or ignore old data and increment number in "GOTO_MRU_2_"
					Insert(filename, image, CStringW(ln.read()));
				}
				else
				{
					// SIW
					Insert(filename, ICONIDX_SIW, NULL);
				}
			}
		}
	}

	m_mru_reg = newKey;
#endif // USE_NEW_MRU_TREE
}

BOOL VAFileView::OnInitDialog()
{
	__super::OnInitDialog();

	auto dpiScope = SetDefaultDpiHelper();

	CRect rc;
	GetClientRect(&rc);
	if (!gShellAttr->IsMsdev())
		rc.top += 3;
	rc.bottom = rc.top + 400;
	rc.right--;
	const uint clipstyle = CVS2010Colours::IsVS2010VAViewColouringActive() ? (WS_CLIPCHILDREN | WS_CLIPSIBLINGS) : 0u;
	m_FilesListBox.Create(WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | CBS_AUTOHSCROLL | CBS_DROPDOWN | WS_TABSTOP |
	                          clipstyle,
	                      rc, this, IDC_GENERICCOMBO);
	m_FilesListBox.SetFontType(VaFontType::EnvironmentFont);

	CRect tmpRc;
	m_FilesListBox.GetWindowRect(&tmpRc);
	// overlap controls by 1 pixel if VAViewColouring is active
	const int diff = tmpRc.Height() - (::GetWinVersion() <= wvWinXP ? 1 : 0) -
	                 (CVS2010Colours::IsVS2010VAViewColouringActive() ? 1 : 0);
	rc.top += diff;
	rc.bottom += diff;
	m_SymbolsListBox.Create(WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | CBS_AUTOHSCROLL | CBS_DROPDOWN | WS_TABSTOP |
	                            clipstyle,
	                        rc, this, IDC_GENERICCOMBO);
	m_SymbolsListBox.SetFontType(VaFontType::EnvironmentFont);

	if (CVS2010Colours::IsVS2010VAViewColouringActive())
	{
		ModifyStyle(0, WS_CLIPCHILDREN);
		m_FilesListBox.highlight_state_change_event =
		    std::bind(&VAFileView::OnHighlightStateChanged, this, std::placeholders::_1);
		m_SymbolsListBox.highlight_state_change_event = m_FilesListBox.highlight_state_change_event;
	}

	// This positioning is important for tabbing among the controls in the VA view (handled in VAWorkspaceViews)
	::SetWindowPos(m_SymbolsListBox.GetSafeHwnd(), HWND_TOP, 0, 0, 0, 0,
	               SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER);
	::SetWindowPos(m_FilesListBox.GetSafeHwnd(), HWND_TOP, 0, 0, 0, 0,
	               SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER);

	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
#ifdef USE_NEW_MRU_TREE
	pList->ModifyStyle(0, TVS_HASBUTTONS | TVS_LINESATROOT);
#endif // USE_NEW_MRU_TREE
	gImgListMgr->SetImgListForDPI(*pList, ImageListManager::bgTree, TVSIL_NORMAL);
	if (gShellAttr->IsMsdev())
	{
		// if you leave style as WS_EX_CLIENTEDGE, make sure to remove
		// the condition before 'treeRc.bottom++'.
		// WS_EX_CLIENTEDGE leaves a bold edge on the left that is
		// more noticeable than the soft edge on the bottom and right
		// with WS_EX_STATICEDGE.
		pList->ModifyStyleEx(WS_EX_CLIENTEDGE, WS_EX_STATICEDGE);
	}
	else if (gShellAttr->IsDevenv11OrHigher() && CVS2010Colours::IsVS2010VAViewColouringActive())
	{
		pList->ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		pList->ModifyStyle(0, TVS_FULLROWSELECT);
	}
	else if (::GetWinVersion() < wvWinXP || gShellAttr->IsDevenv7())
	{
		// <= vs2003 in xp or vs200x in win2000
		pList->ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		pList->ModifyStyle(0, WS_BORDER);
	}

	m_SymbolsListBox.GetWindowRect(&tmpRc);
	CRect treeRc;
	pList->GetWindowRect(&treeRc);
	treeRc.top = tmpRc.bottom;
	if (!gShellAttr->IsMsdev())
		treeRc.bottom++;
	ScreenToClient(treeRc);
	pList->MoveWindow(treeRc);

	AddSzControl(IDC_TREE1, mdResize, mdResize);
	AddSzControl(m_FilesListBox, mdResize, mdNone);
	AddSzControl(m_SymbolsListBox, mdResize, mdNone);

	// [case: 77373]
	LoadState(WTString(sLastProjectLoad).c_str());
	EnableToolTips();
	pList->EnableToolTips();
	new VAFileView_TreeCtrl(
	    pList->m_hWnd,
	    CVS2010Colours::IsVS2010VAViewColouringActive()
	        ? NULL
	        : "List of most recently used files and symbols. Right-click to clear list or change options.",
	    22);
	if (CVS2010Colours::IsVS2010VAViewColouringActive())
		::mySetProp(pList->m_hWnd, "__VA_do_not_colour", (HANDLE)1);

	CToolTipCtrl* tips = pList->GetToolTips();
	if (tips)
		tips->SetWindowPos(&pList->wndTopMost, 0, 0, VsUI::DpiHelper::LogicalToDeviceUnitsX(10), VsUI::DpiHelper::LogicalToDeviceUnitsY(10), SWP_NOACTIVATE);

#ifdef USE_NEW_MRU_TREE
	sMRU.mView = this;
	sMRU.UpdateTree();
#endif // USE_NEW_MRU_TREE

	return TRUE; // return TRUE unless you set the focus to a control
	             // EXCEPTION: OCX Property Pages should return FALSE
}

void VAFileView::Insert(CStringW txt, int image, LPCWSTR param, UINT flags /* = 0*/)
{
#if !defined(USE_NEW_MRU_TREE)
	CStringW tparam(param);
	sLastMruScope.Empty();
	sLastMruFile.Empty();
	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	if (!param)
	{
		if (wcschr(L":.", *txt))
			txt = txt.Mid(1);
		txt.Replace(L':', L'.');
	}
	_ASSERTE(txt.GetLength());
	if (!txt.GetLength())
		return;

	// remove dupes
	HTREEITEM item = pList->GetRootItem();
	while (item)
	{
		HTREEITEM litem = item;
		item = pList->GetNextItem(item, TVGN_NEXT);
		CStringW s = ::GetItemTextW(pList, litem);
		if (_wcsicmp(s, txt) == 0)
		{
			CStringW* pstr = (CStringW*)pList->GetItemData(litem);
			int oImg, oSelImg;
			pList->GetItemImage(litem, oImg, oSelImg);
			if (image == 12 || // don't set to focused
			    oImg == 18     // leave edited image
			)
				image = oImg;
			pList->DeleteItem(litem);
			if (pstr) // delete after item is removed
				delete pstr;
		}
	}
	_ASSERTE(!param || tparam.GetLength());
	if (gTestLogger && gTestLogger->IsMruLoggingEnabled())
	{
		WTString msg;
		msg.WTFormat("MRU Insert: %x %x %ls", image, flags, (LPCWSTR)txt);
		gTestLogger->LogStr(msg);
	}

	_ASSERTE(pList);
	TVINSERTSTRUCTW tvis;
	tvis.hParent = TVI_ROOT;
	tvis.hInsertAfter = TVI_FIRST;
	tvis.item.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN | TVIF_TEXT | TVIF_PARAM;
	tvis.item.pszText = (LPWSTR)(LPCWSTR)txt;
	tvis.item.iImage = image;
	tvis.item.iSelectedImage = image;
	tvis.item.state = 0;
	tvis.item.stateMask = TVIS_SELECTED | flags;
	tvis.item.lParam = param ? (LPARAM) new CStringW(tparam) : NULL;
	HTREEITEM titem = (HTREEITEM)::SendMessageW(pList->m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);

	pList->EnsureVisible(titem);

	// [case: 56634]
	SCROLLINFO si;
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_POS;
	si.nPos = 0;
	if (pList->GetScrollInfo(SB_HORZ, &si) && si.nPos)
		pList->SendMessage(WM_HSCROLL, SB_THUMBPOSITION, 0);
#endif // USE_NEW_MRU_TREE
}

static HTREEITEM s_selItem = NULL;

LRESULT
VAFileView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	try
	{
		// TODO: Add your specialized code here and/or call the base class
#ifdef USE_NEW_MRU_TREE
		if (message == WM_COMMAND && wParam == ID_REFRESH)
		{
			sMRU.UpdateTree();
			return TRUE;
		}
		if (message == WM_COMMAND && wParam == IDC_INCLUDE_ALL_RECENT_FILES)
		{
			Psettings->m_nMRUOptions ^= IDC_INCLUDE_ALL_RECENT_FILES_BIT;
			sMRU.UpdateTree();
			return TRUE;
		}
#endif // USE_NEW_MRU_TREE
		if (message == WM_VSCROLL || message == WM_HSCROLL)
		{
			SetFocus();
		}
		if (message == WM_COMMAND && wParam == IDC_INCLUDEALLEDITEDFILES)
		{
			Psettings->m_nMRUOptions ^= 0x1;
			return TRUE;
		}
		if (message == WM_COMMAND && wParam == IDC_ADDFILESWITHFOCUS)
		{
			Psettings->m_nMRUOptions ^= 0x4;
			return TRUE;
		}
		if (message == WM_COMMAND && wParam == ID_DUMMY_INCLUDEMODIFIEDMETHODS)
		{
			Psettings->m_nMRUOptions ^= 0x8;
			return TRUE;
		}
		if (message == WM_COMMAND && wParam == IDC_GOTO)
		{
			OnGoto();
			return TRUE;
		}
		if (message == WM_COMMAND && wParam == IDC_CLEAR)
		{
			CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
			if (s_selItem)
			{
#ifdef USE_NEW_MRU_TREE
				int itemdata = (int)pList->GetItemData(s_selItem);
				sMRU.Clear(itemdata);
				return TRUE;
#else
				CStringW* itemData = (CStringW*)pList->GetItemData(s_selItem);
				pList->DeleteItem(s_selItem);
				delete itemData;
#endif // USE_NEW_MRU_TREE
			}
			sLastMruFile.Empty();
			return TRUE;
		}
#ifdef USE_NEW_MRU_TREE
		if (message == WM_COMMAND && wParam == IDC_CLEARALL)
		{
			sMRU.Clear();
			return TRUE;
		}
#endif // USE_NEW_MRU_TREE
		if (message == WM_COMMAND && wParam == IDC_CLEARALL)
		{
			CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
			HTREEITEM item = pList->GetChildItem(TVI_ROOT);
			while (item)
			{
				CStringW* itemData = (CStringW*)pList->GetItemData(item);
				delete itemData;
				item = pList->GetNextItem(item, TVGN_NEXT);
			}
			pList->DeleteAllItems();
			sLastMruFile.Empty();
			return TRUE;
		}
		if (message == WM_VA_GOTOCLASSVIEWITEM)
		{
			WTString sym((LPCSTR)lParam);
			if (sym.GetLength())
			{
				token t = sym;
				t.ReplaceAll(TRegexp("<.*>"), string(""));
				WTString cls, meth;
				while (t.more())
				{
					cls = meth;
					meth = t.read(DB_SEP_STR2);
				}
				if (cls.GetLength())
					meth = cls + "." + meth;
				Insert(meth.Wide(), ICONIDX_SIW /*wParam*/, sym.Wide());
			}
			return TRUE;
		}
		if (message == WM_NCPAINT)
		{
			// when window is autohide, hitting escape can leave tooltip orphan
			if (mTimerId)
				KillTimer(mTimerId);
			mTimerId = SetTimer(kTimerId_ClearTooltip, 2000u, NULL);
		}
		if (message == WM_TIMER)
		{
			if (wParam == mTimerId)
			{
				mTimerId = 0;
				KillTimer(wParam);
				CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
				if (pList->GetSafeHwnd())
				{
					CToolTipCtrl* tips = pList->GetToolTips();
					if (tips->GetSafeHwnd() && ::IsWindow(tips->m_hWnd) && tips->IsWindowVisible())
						tips->ShowWindow(SW_HIDE);
				}
				return TRUE;
			}
		}
		if (message == WM_DESTROY)
		{
			if (mTimerId)
				KillTimer(mTimerId);
			LoadState("GOTO_MRU_DEST", TRUE);
		}
	}
	catch (...)
	{
		VALOGEXCEPTION("VAFV:");
	}
	if (message == WM_ERASEBKGND && CVS2010Colours::IsVS2010VAViewColouringActive())
	{
		COLORREF clr = g_IdeSettings->GetEnvironmentColor(L"CommandBarGradientBegin", false);
		if (UINT_MAX != clr)
		{
			CRect crect;
			GetClientRect(&crect);
			CBrush brush;
			brush.CreateSolidBrush(clr);
			::FillRect((HDC)wParam, crect, brush);
			return true;
		}
	}

	return __super::WindowProc(message, wParam, lParam);
}

void VAFileView::OnMruDoubleClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	OnGoto();
	*pResult = TRUE;
}

void VAFileView::OnGoto()
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftMru);
	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
#ifdef USE_NEW_MRU_TREE
	int itemdata = (int)pList->GetItemData(pList->GetSelectedItem());
	sMRU.GotoItem(itemdata);
#else
	CStringW* paramStr = (CStringW*)pList->GetItemData(pList->GetSelectedItem());
	CStringW txt = ::GetItemTextW(pList, pList->GetSelectedItem());
	int img, sel;
	pList->GetItemImage(pList->GetSelectedItem(), img, sel);
	if (paramStr && !IsBadReadPtr(paramStr, sizeof(CStringW*)))
	{
		try
		{
			CStringW theItem(*paramStr);
			if (theItem.GetLength())
			{
				if (IsFile(theItem))
				{
					DelayFileOpen(theItem, 0, nullptr);
					Insert(Basename(theItem), img, theItem);
					return;
				}

				// sym
				if (GoToDEF(WTString(theItem)))
				{
					Insert(txt, img, theItem);
					return;
				}

				// failed, try opening up scope - strip last param
				// it could be a member like :class:method:member
				const int pos = theItem.ReverseFind(L':');
				if (-1 != pos)
				{
					const WTString tmpItem = theItem.Left(pos);
					if (GoToDEF(tmpItem))
					{
						Insert(txt, img, theItem);
						return;
					}
				}
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("VAFV:");
			_ASSERTE(!"VAFileView::OnGoto exception caught");
		}
	}

	if (txt.Find(L':') != 0)
		txt = L':' + txt;
	WTString wtxt = txt;
	wtxt.Replace(".", ":");
	GoToDEF(wtxt);
	Insert(txt, img, NULL);
#endif // USE_NEW_MRU_TREE
}

HTREEITEM
VAFileView::GetItemFromPos(POINT pos) const
{
	TVHITTESTINFO ht = {{0}};
	ht.pt = pos;
	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	::MapWindowPoints(HWND_DESKTOP, pList->m_hWnd, &ht.pt, 1);

	if (pList->HitTest(&ht) && (ht.flags & TVHT_ONITEM))
		return ht.hItem;

	return NULL;
}

void VAFileView::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftMru);
	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	HTREEITEM selItem = pList->GetSelectedItem();

	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && selItem)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (pList->GetItemRect(selItem, &rc, TRUE))
		{
			pList->ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}

	DisplayContextMenu(pos);
}

void VAFileView::OnMruRightClick(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftMru);
	*pResult = 0; // let the tree send us a WM_CONTEXTMENU msg

	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	const CPoint pt(GetCurrentMessage()->pt);
	HTREEITEM it = GetItemFromPos(pt);
	if (it)
		pList->SelectItem(it);
}

void VAFileView::OnMruLeftClick(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftMru);
	*pResult = 0;
}

void VAFileView::DisplayContextMenu(CPoint pos)
{
	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	s_selItem = pList->GetDropHilightItem();
	if (!s_selItem)
		s_selItem = pList->GetSelectedItem();

	//	SetForegroundWindow();

	CMenu parentMenu;
#ifdef USE_NEW_MRU_TREE
	parentMenu.LoadMenu(IDR_FILEVIEW_MENU2);
	CMenu* contextMenu(parentMenu.GetSubMenu(0));
	if (Psettings->m_nMRUOptions & IDC_INCLUDE_ALL_RECENT_FILES_BIT)
		contextMenu->CheckMenuItem(IDC_INCLUDE_ALL_RECENT_FILES, MF_BYCOMMAND | MF_CHECKED);
#else
	parentMenu.LoadMenu(IDR_FILEVIEW_MENU);
	CMenu* contextMenu(parentMenu.GetSubMenu(0));

	if (Psettings->m_nMRUOptions & 0x4)
	{
		contextMenu->CheckMenuItem(IDC_ADDFILESWITHFOCUS, MF_BYCOMMAND | MF_CHECKED);
		contextMenu->EnableMenuItem(IDC_INCLUDEALLEDITEDFILES, MF_DISABLED | MF_GRAYED);
	}
	if (Psettings->m_nMRUOptions & 0x1)
		contextMenu->CheckMenuItem(IDC_INCLUDEALLEDITEDFILES, MF_BYCOMMAND | MF_CHECKED);
	if (Psettings->m_nMRUOptions & 0x8)
		contextMenu->CheckMenuItem(ID_DUMMY_INCLUDEMODIFIEDMETHODS, MF_BYCOMMAND | MF_CHECKED);
#endif // USE_NEW_MRU_TREE

	CMenuXP::SetXPLookNFeel(this, contextMenu != NULL);
	MenuXpHook hk(this);
	contextMenu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
}

BOOL VAFileView::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	if (1)
	{
		NMHDR* pNMHDR = (NMHDR*)lParam;
#ifdef USE_NEW_MRU_TREE
		if (sMRU.OnNotify(pNMHDR))
			return TRUE;
#endif // USE_NEW_MRU_TREE
       //		if (pNMHDR->code == EN_CHANGE)
       //		{
       //			int n = 123;
       //		}
		if (pNMHDR->code == TVN_GETINFOTIPA)
		{
#ifdef USE_NEW_MRU_TREE
			// TODO: get tip from MRU
			*pResult = FALSE;
			return TRUE;
#else
			CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
			//			TOOLTIPTEXT* pTTTW = (TOOLTIPTEXT*)pNMHDR;
			LPNMTVGETINFOTIP lpGetInfoTipw = (LPNMTVGETINFOTIP)lParam;
			//			LPNMTTDISPINFO lpnmtdi = (LPNMTTDISPINFO) lParam;

			WTString txt;
			try
			{
				EdCntPtr ed(g_currentEdCnt);
				MultiParsePtr mp = ed ? ed->GetParseDb() : nullptr;
				HTREEITEM i = lpGetInfoTipw->hItem;
				CStringW* pfile = (CStringW*)pList->GetItemData(i);
				if (pfile)
				{
					txt = CString(*pfile);
					if (!IsFile(*pfile))
					{
						DType* data = mp ? mp->FindExact(txt) : nullptr;
						if (data)
						{
							data->LoadStrs();
							WTString cmnt = GetCommentForSym(data->SymScope());
							if (cmnt.GetLength())
								cmnt.prepend("\n");
							txt = DecodeScope(data->Def()) + cmnt;
						}
					}
				}
				else
				{
					const WTString sym = ::GetItemTextW(pList, i);
					DType* data = mp ? mp->FindExact(sym) : nullptr;
					if (data)
					{
						data->LoadStrs();
						WTString cmnt = GetCommentForSym(data->SymScope());
						if (cmnt.GetLength())
							cmnt.prepend("\n");
						txt = DecodeScope(data->Def()) + cmnt;
					}
				}
			}
			catch (...)
			{
				VALOGEXCEPTION("VAFV:");
			}

			LPCSTR p = txt.c_str();
			for (int n = 0; n < lpGetInfoTipw->cchTextMax && p[n]; n++)
			{
				lpGetInfoTipw->pszText[n] = p[n];
				lpGetInfoTipw->pszText[n + 1] = '\0';
			}
			return TRUE;
#endif // USE_NEW_MRU_TREE
		}
		else if (pNMHDR->code == TVN_GETINFOTIPW)
		{
#ifdef USE_NEW_MRU_TREE
			// TODO: get tip from MRU
			*pResult = FALSE;
			return TRUE;
#else
			CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
			//			TOOLTIPTEXT* pTTTW = (TOOLTIPTEXT*)pNMHDR;
			LPNMTVGETINFOTIPW lpGetInfoTipw = (LPNMTVGETINFOTIPW)lParam;
			//			LPNMTTDISPINFOW lpnmtdi = (LPNMTTDISPINFOW)lParam;

			WTString txt;
			try
			{
				EdCntPtr ed(g_currentEdCnt);
				MultiParsePtr mp = ed ? ed->GetParseDb() : nullptr;
				HTREEITEM i = lpGetInfoTipw->hItem;
				CStringW* pfile = (CStringW*)pList->GetItemData(i);
				if (pfile)
				{
					txt = *pfile;
					if (!IsFile(*pfile))
					{
						DType* data = mp ? mp->FindExact(txt) : nullptr;
						if (data)
						{
							data->LoadStrs();
							WTString cmnt = GetCommentForSym(data->SymScope());
							if (cmnt.GetLength())
								cmnt.prepend("\n");
							txt = DecodeScope(data->Def()) + cmnt;
						}
					}
				}
				else
				{
					const CStringW sym = ::GetItemTextW(pList, i);
					DType* data = mp ? mp->FindExact(sym) : nullptr;
					if (data)
					{
						data->LoadStrs();
						WTString cmnt = GetCommentForSym(data->SymScope());
						if (cmnt.GetLength())
							cmnt.prepend("\n");
						txt = DecodeScope(data->Def()) + cmnt;
					}
				}
			}
			catch (...)
			{
				VALOGEXCEPTION("VAFV:");
			}

			CStringW wTxt = txt.Wide();
			for (int n = 0; n < lpGetInfoTipw->cchTextMax && wTxt[n]; n++)
			{
				lpGetInfoTipw->pszText[n] = wTxt[n];
				lpGetInfoTipw->pszText[n + 1] = '\0';
			}
			return TRUE;
#endif // USE_NEW_MRU_TREE
		}
	}
	return __super::OnNotify(wParam, lParam, pResult);
}

void VAFileView::OnDpiChanged(DpiChange change, bool& handled)
{
	__super::OnDpiChanged(change, handled);

	if (change == CDpiAware::DpiChange::AfterParent)
	{
		// Update layout

		CRect flRc;
		m_FilesListBox.GetWindowRect(flRc);
		ScreenToClient(flRc);

		CRect slRc;
		m_SymbolsListBox.GetWindowRect(&slRc);
		ScreenToClient(slRc);

		slRc.bottom = flRc.bottom + slRc.Height();
		slRc.top = flRc.bottom;
		m_SymbolsListBox.MoveWindow(&slRc);

		CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
		if (pList)
		{
			CRect treeRc;
			pList->GetWindowRect(&treeRc);
			ScreenToClient(treeRc);

			treeRc.top = slRc.bottom;

			if (!gShellAttr->IsMsdev())
				treeRc.bottom++;
			else
			{
				CRect cr;
				GetClientRect(&cr);
				treeRc.bottom = cr.bottom;
			}

			pList->MoveWindow(treeRc);
		}

		ResetLayout();
	}
}

void VAFileView::SetFocusToFis()
{
	m_FilesListBox.SetFocus();
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftFis);
}

void VAFileView::SetFocusToSis()
{
	m_SymbolsListBox.SetFocus();
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftSis);
}

void VAFileView::GotoFiles(bool showDropdown)
{
	if (m_FilesListBox.m_hWnd)
	{
		if (m_SymbolsListBox.GetDroppedState())
			m_SymbolsListBox.DisplayList(false);
		m_FilesListBox.SetFocus();
		if (showDropdown)
			m_FilesListBox.OnDropdown();
	}
}

void VAFileView::GotoSymbols(bool showDropdown)
{
	if (m_SymbolsListBox.m_hWnd)
	{
		if (m_FilesListBox.GetDroppedState())
			m_FilesListBox.DisplayList(false);
		m_SymbolsListBox.SetFocus();
		if (showDropdown)
			m_SymbolsListBox.OnDropdown();
	}
}

void VAFileView::Layout(bool dpiChanged /*= false*/)
{
	__super::Layout();

	if (dpiChanged)
	{
		CRect tmpRc;
		m_SymbolsListBox.GetWindowRect(&tmpRc);
		// overlap controls by 1 pixel if VAViewColouring is active
		const int diff = tmpRc.Height() - (::GetWinVersion() <= wvWinXP ? 1 : 0) -
		                 (CVS2010Colours::IsVS2010VAViewColouringActive() ? 1 : 0);
		tmpRc.top -= diff;
		tmpRc.bottom -= diff;
		ScreenToClient(&tmpRc);
		m_FilesListBox.MoveWindow(&tmpRc, FALSE);
	}
}

void VAFileView::OnHighlightStateChanged(uint newstate) const
{
	// won't be called in non-VS2010, but we'll make the check anyways
	if (!CVS2010Colours::IsVS2010VAViewColouringActive())
		return;

	// basically, we'll put combo box with focus in front to display shared border line as highlighed
	bool files_highlighted = !!(((uint)(uintptr_t)::myGetProp(m_FilesListBox.m_hWnd, "last_highlight_state")) &
	                            ComboPaintSubClass::highlighted_draw_highlighted);
	bool symbols_highlighted = !!(((uint)(uintptr_t)::myGetProp(m_SymbolsListBox.m_hWnd, "last_highlight_state")) &
	                              ComboPaintSubClass::highlighted_draw_highlighted);
	if (files_highlighted == symbols_highlighted)
		return;
	bool put_files_above_symbols = files_highlighted;

	// check current situation
	bool files_above_symbols = true;
	for (HWND child = ::GetWindow(m_hWnd, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
	{
		if (child == m_FilesListBox.m_hWnd)
		{
			files_above_symbols = true;
			break;
		}
		else if (child == m_SymbolsListBox.m_hWnd)
		{
			files_above_symbols = false;
			break;
		}
	}

	if (put_files_above_symbols != files_above_symbols)
	{
		if (put_files_above_symbols)
		{
			//			OutputDebugStringA("FEC: files above symbols");
			const_cast<VAFileView*>(this)->m_FilesListBox.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
			                                                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		else
		{
			//			OutputDebugStringA("FEC: symbols above files");
			const_cast<VAFileView*>(this)->m_SymbolsListBox.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
			                                                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}
}

BOOL VAFileView::OnEraseBkgnd(CDC* dc)
{
	if (CVS2010Colours::IsVS2010VAViewColouringActive() && gPkgServiceProvider)
		return 1;
	else
		return __super::OnEraseBkgnd(dc);
}

void VAFileView::SettingsChanged()
{
	CTreeCtrl* pTree = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	if (pTree)
		gImgListMgr->SetImgListForDPI(*pTree, ImageListManager::bgTree, TVSIL_NORMAL);
	m_FilesListBox.SettingsChanged();
	m_SymbolsListBox.SettingsChanged();
	Invalidate(TRUE);
}

void VAFileView::SetFocusToMru()
{
	CTreeCtrl* pList = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	if (pList->GetSafeHwnd())
		pList->SetFocus();
	VAWorkspaceViews::SetFocusTarget(VAWorkspaceViews::ftMru);
}
