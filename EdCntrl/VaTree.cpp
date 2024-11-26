// VaTree.cpp : implementation file
//

#include "stdafxed.h"
#include "expansion.h"
#include "VaTree.h"
#if _MSC_VER <= 1200
#include <../src/afximpl.h>
#else
#include <../atlmfc/src/mfc/afximpl.h>
#endif
#include "..\addin\DSCmds.h"
#include "DevShellAttributes.h"
#include "wt_stdlib.h"
#include "Settings.h"
#include "file.h"
#include "Directories.h"
#include "DevShellService.h"
#include "TokenW.h"
#include "FeatureSupport.h"
#include "StringUtils.h"
#include "ImageListManager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define METHODFILE VaDirs::GetUserDir() + L"misc/VAssist.mthw"
#define FILEFILE VaDirs::GetUserDir() + L"misc/VAssist.filw"
#define COPYFILE VaDirs::GetUserDir() + L"misc/VAssist.cpyw"
#define BKMKFILE VaDirs::GetUserDir() + L"misc/VAssist.bmkw"

#if _MSC_VER > 1200
using std::ifstream;
using std::ios;
using std::ofstream;
#endif

using OWL::string;
using OWL::TRegexp;

static HTREEITEM g_curItem;
VaTree* g_VATabTree = nullptr;

void VATreeSetup(bool init)
{
	static CWnd* sTreeParent = nullptr;

	if (!init)
	{
		if (g_VATabTree)
		{
			delete g_VATabTree;
			g_VATabTree = nullptr;
		}
		if (sTreeParent)
		{
			CHandleMap* pMap = afxMapHWND();
			if (!pMap || !IsWindow(sTreeParent->m_hWnd))
				sTreeParent->m_hWnd = nullptr;
			sTreeParent->DestroyWindow();
			delete sTreeParent;
			sTreeParent = nullptr;
		}
	}
	else if (!g_VATabTree)
	{
		sTreeParent = new CWnd;
		CRect rc(0, 0, 0, 0);
		sTreeParent->CreateEx(0, AfxRegisterWndClass(0), "VATreeParent", 0, rc, nullptr, 0);
		g_VATabTree = new VaTree;
		g_VATabTree->Create(sTreeParent, 102);
		g_VATabTree->SetWindowText("Visual Assist");
	}
}

class FilePos
{
  public:
	CStringW m_file;
	int m_ln;
	FilePos(const CStringW& file, int line)
	{
		m_file = file;
		m_ln = line;
	}
};

struct CopyData
{
	CopyData(LPCWSTR data) : mData(data), mHash(WTHashKeyW(data))
	{
	}

	CStringW mData;
	UINT mHash;
};

/////////////////////////////////////////////////////////////////////////////
// VaTree

VaTree::VaTree()
{
	m_insOrder = TVI_FIRST;
}

VaTree::~VaTree()
{
	if (m_hWnd)
	{
		Shutdown();
		CHandleMap* pMap = afxMapHWND();
		if (!pMap || !IsWindow(m_hWnd))
			m_hWnd = nullptr;
	}
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(VaTree, CTreeCtrl)
//{{AFX_MSG_MAP(VaTree)
ON_WM_LBUTTONDBLCLK()
ON_NOTIFY_REFLECT(NM_CLICK, OnClick)
ON_WM_RBUTTONDOWN()
ON_WM_DESTROY()
ON_WM_KEYDOWN()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#pragma warning(pop)

/////////////////////////////////////////////////////////////////////////////
// VaTree message handlers

void VaTree::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	CTreeCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
	if (VK_RETURN == nChar)
		OpenItemFile(GetSelectedItem(), nullptr);
}

void VaTree::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	CTreeCtrl::OnLButtonDblClk(nFlags, point);
	HTREEITEM item = GetSelectedItem();
	if (item == HitTest(point))
		OpenItemFile(item, nullptr);
}

void VaTree::OnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	// Add your control notification handler code here
	*pResult = 0;
}

void VaTree::AddMethod(LPCSTR method, const CStringW& file, int line, bool addFileAndLine /*= true*/)
{
	g_curItem = nullptr;
	static int lstln;
	static CStringW lfile;
	if (lfile == file && abs(line - lstln) < 25)
		return;
	lstln = line;
	lfile = file;
	CStringW txt;
	if (addFileAndLine)
	{
		token2 t = method;
		txt = t.read(DB_SEP_STR2).Wide();
		if (t.more() > 2)
			txt = txt + L"." + CStringW(t.read().Wide());
	}
	else
	{
		if (method[0] == ':')
			txt = &method[1];
		else
			txt = method;
	}
	if (!txt.GetLength() || txt == L"PP" || method[0] != ':')
	{
		// global/preproc scope
		// AddFile(file, line);
		// return;
		txt.Empty();
	}
	if (txt.GetLength())
		txt = CleanScopeForDisplay(txt).Wide();
	if (addFileAndLine)
		txt += (txt.GetLength() ? CStringW(L" in ") : CStringW(L"")) + Basename(file) + L":" + itosw(line);
	HTREEITEM last = GetNextItem(m_methods, TVGN_CHILD);
	if (last && !GetItemTextW(last).CompareNoCase(txt))
	{
		// same as last just update line and return
		FilePos* fp = (FilePos*)GetItemData(last);
		fp->m_ln = line;
		return;
	}
	SetRedraw(false);
	HTREEITEM sav, rec = InsertItemW(txt, 7, 8, m_methods, m_insOrder);
	FilePos* fp = new FilePos(file, line);
	SetItemData(rec, (DWORD_PTR)fp);
	// delete duplicates
	sav = rec;
	while (rec)
	{
		rec = GetNextItem(rec, TVGN_NEXT);
		if (rec && !GetItemTextW(rec).CompareNoCase(txt))
		{
			DeleteItem(rec);
			break;
		}
	}
	rec = sav;
	// limit item count
	for (uint n = Psettings->m_RecentCnt - 1; rec; n--)
	{
		rec = GetNextItem(rec, TVGN_NEXT);
		if (n < 1 && rec)
		{
			DeleteItem(rec);
			break;
		}
	}
	AddFile(file, line);
	if (!m_methodExp)
	{
		Expand(m_methods, TVE_EXPAND);
		m_methodExp = true;
	}
	SetRedraw();
}

void VaTree::DeleteItem(HTREEITEM item)
{
	ASSERT(item != nullptr);
	FilePos* fp = (FilePos*)GetItemData(item);
	delete fp;
	CTreeCtrl::DeleteItem(item);
}

void VaTree::DeleteCopyItem(HTREEITEM item)
{
	ASSERT(item != nullptr);
	CopyData* fp = (CopyData*)GetItemData(item);
	delete fp;
	CTreeCtrl::DeleteItem(item);
}

void VaTree::Create(CWnd* parent, int id)
{
	CRect r;
	parent->GetClientRect(r);
	CTreeCtrl::Create(DWORD(WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES), r, parent, (UINT)id);
	gImgListMgr->SetImgListForDPI(*this, ImageListManager::bgTree, TVSIL_NORMAL);
	ModifyStyleEx(0, WS_EX_CLIENTEDGE);
	HTREEITEM root = InsertItem(TVIF_IMAGE | TVIF_PARAM | TVIF_SELECTEDIMAGE | TVIF_TEXT, "Visual Assist Workspace", 0,
	                            0, TVIS_BOLD, TVIS_BOLD, NULL, TVI_ROOT, TVI_LAST);
	m_copybuf = InsertItem(TVIF_IMAGE | TVIF_PARAM | TVIF_SELECTEDIMAGE | TVIF_TEXT, VAT_PASTE, 0, 0, TVIS_BOLD,
	                       TVIS_BOLD, NULL, root, TVI_LAST);
	m_methods = InsertItem(TVIF_IMAGE | TVIF_PARAM | TVIF_SELECTEDIMAGE | TVIF_TEXT, VAT_METHOD, 0, 0, TVIS_BOLD,
	                       TVIS_BOLD, NULL, root, TVI_LAST);
	m_files = InsertItem(TVIF_IMAGE | TVIF_PARAM | TVIF_SELECTEDIMAGE | TVIF_TEXT, VAT_FILE, 0, 0, TVIS_BOLD, TVIS_BOLD,
	                     NULL, root, TVI_LAST);
	m_bookmarks = InsertItem(TVIF_IMAGE | TVIF_PARAM | TVIF_SELECTEDIMAGE | TVIF_TEXT, VAT_BOOKMARK, 0, 0, TVIS_BOLD,
	                         TVIS_BOLD, NULL, root, TVI_LAST);

	m_insOrder = TVI_LAST;
	m_methodExp = m_fileExp = m_copyExp = m_bookmarkExp = false;
	int line;
	int pos = 0;
	CStringW file, fileContents;

	if (::ReadFileUtf16(METHODFILE, fileContents))
	{
		WTString method;
		pos = 0;
		while (pos < fileContents.GetLength())
		{
			int nextPos = fileContents.Find(L"|", pos);
			if (-1 == nextPos)
				break;
			method = fileContents.Mid(pos, nextPos - pos);
			pos = nextPos + 1;

			nextPos = fileContents.Find(L"|", pos);
			if (-1 == nextPos)
				nextPos = fileContents.GetLength();
			file = fileContents.Mid(pos, nextPos - pos);
			pos = nextPos + 1;

			nextPos = fileContents.Find(L"\n", pos);
			if (-1 == nextPos)
				nextPos = fileContents.GetLength();
			line = ::_wtoi(fileContents.Mid(pos, nextPos - pos));
			pos = nextPos + 1;

			// don't auto add the file and line to the method string
			AddMethod(method.c_str(), file, line, false);
		}
	}

	if (::ReadFileUtf16(FILEFILE, fileContents))
	{
		pos = 0;
		while (pos < fileContents.GetLength())
		{
			int nextPos = fileContents.Find(L"|", pos);
			if (-1 == nextPos)
				break;
			file = fileContents.Mid(pos, nextPos - pos);
			pos = nextPos + 1;

			nextPos = fileContents.Find(L"\n", pos);
			if (-1 == nextPos)
				nextPos = fileContents.GetLength();
			line = ::_wtoi(fileContents.Mid(pos, nextPos - pos));
			pos = nextPos + 1;

			AddFile(file, line);
		}
	}

	ReadPasteHistory();

	if (Psettings->m_keepBookmarks && gShellAttr->SupportsBookmarks())
	{
		if (::ReadFileUtf16(BKMKFILE, fileContents))
		{
			pos = 0;
			while (pos < fileContents.GetLength())
			{
				int nextPos = fileContents.Find(L"|", pos);
				if (-1 == nextPos)
					break;
				file = fileContents.Mid(pos, nextPos - pos);
				pos = nextPos + 1;

				nextPos = fileContents.Find(L"\n", pos);
				if (-1 == nextPos)
					nextPos = fileContents.GetLength();
				line = ::_wtoi(fileContents.Mid(pos, nextPos - pos));
				pos = nextPos + 1;

				ToggleBookmark(file, line);
			}
		}
	}

	m_insOrder = TVI_FIRST;
	Expand(root, TVE_EXPAND);
	if (m_methods && Expand(m_methods, TVE_EXPAND))
		m_methodExp = true;
	if (m_files && Expand(m_files, TVE_EXPAND))
		m_fileExp = true;
	if (m_bookmarks && Expand(m_bookmarks, TVE_EXPAND))
		m_bookmarkExp = true;
	if (m_copybuf && Expand(m_copybuf, TVE_EXPAND))
		m_copyExp = true;
}

CStringW VaTree::GetClipboardItem(int clipIdx) const
{
	CStringW clipboardItem;

	if (m_copybuf)
	{
		HTREEITEM item = g_VATabTree->GetChildItem(m_copybuf);
		for (int idx = 0; item; ++idx)
		{
			if (idx == clipIdx)
			{
				DWORD_PTR data = GetItemData(item);
				if (data)
				{
					CopyData* cp = (CopyData*)data;
					clipboardItem = cp->mData;
				}
				break;
			}
			item = g_VATabTree->GetNextItem(item, TVGN_NEXT);
		}
	}

	return clipboardItem;
}

void VaTree::OpenItemFile(HTREEITEM item, EdCnt* ed)
{
	const HTREEITEM hpar = GetNextItem(item, TVGN_PARENT);
	DWORD_PTR data = GetItemData(item);
	if (!data)
		return; // parent

	FilePos* fp = (FilePos*)data;
	if (hpar == m_copybuf)
	{
		CopyData* cp = (CopyData*)data;
		if (Psettings->m_clipboardCnt && ::OpenClipboard(m_hWnd))
		{
			// add to clipboard and then call paste
			try
			{
				HGLOBAL hData =
				    GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (cp->mData.GetLength() + 1) * sizeof(wchar_t));
				LPVOID pTxt = GlobalLock(hData);
				memcpy(pTxt, (LPCWSTR)cp->mData, (cp->mData.GetLength() + 1) * sizeof(wchar_t));

				EmptyClipboard();
				SetClipboardData(CF_UNICODETEXT, hData);
				CloseClipboard();
				// The Clipboard now owns the allocated memory and will delete this
				//  data object when new data is put on the Clipboard
				GlobalUnlock(hData);
			}
			catch (...)
			{
				VALOGEXCEPTION("VAT:");
				ASSERT(!"clipboard failure?");
			}

			long pastePos = 0;
			if (IsFeatureSupported(Feature_FormatAfterPaste))
				pastePos = ed->GetSelBegPos();
			ed->InsertW(cp->mData, true, noFormat);
			if (IsFeatureSupported(Feature_FormatAfterPaste))
			{
				if (::HasUnformattableMultilineCStyleComment(cp->mData))
					SetStatus("No 'Format after paste' due to block comment in pasted text");
				else
				{
					ed->SetSelection(pastePos, (long)ed->CurPos());
					gShellSvc->FormatSelection();
					ed->SetPos(ed->CurPos());
				}
			}
		}
	}
	else
	{
		DelayFileOpen(fp->m_file, fp->m_ln + 1, nullptr, TRUE);
	}

	// move to top of list
	if (TVI_FIRST == m_insOrder && m_bookmarks != hpar)
	{
		int img1, img2;
		GetItemImage(item, img1, img2);
		CStringW txt = GetItemTextW(item);
		SetRedraw(false);
		CTreeCtrl::DeleteItem(item);
		HTREEITEM h = InsertItemW(txt, img1, img2, hpar, m_insOrder);
		SetRedraw();
		SetItemData(h, data);
	}
}

void VaTree::AddFile(const CStringW& file, int line)
{
	CStringW txt = Basename(file);
	HTREEITEM last = GetNextItem(m_files, TVGN_CHILD);
	if (last && !GetItemTextW(last).CompareNoCase(txt))
	{
		// same as last just update line and return
		FilePos* fp = (FilePos*)GetItemData(last);
		fp->m_ln = line;
		return;
	}
	SetRedraw(false);
	HTREEITEM sav, rec = InsertItem(CString(txt), 7, 8, m_files, m_insOrder);

	FilePos* fp = new FilePos(file, line);
	SetItemData(rec, (DWORD_PTR)fp);
	// delete duplicates
	sav = rec;
	while (rec)
	{
		rec = GetNextItem(rec, UINT(m_insOrder == TVI_FIRST ? TVGN_NEXT : TVGN_PREVIOUS));
		if (rec && !GetItemTextW(rec).CompareNoCase(txt))
		{
			DeleteItem(rec);
			break;
		}
	}
	rec = sav;
	// limit item count
	for (uint n = Psettings->m_RecentCnt - 1; rec; n--)
	{
		rec = GetNextItem(rec, TVGN_NEXT);
		if (n < 1 && rec)
		{
			DeleteItem(rec);
			break;
		}
	}
	if (!m_fileExp)
	{
		Expand(m_files, TVE_EXPAND);
		m_fileExp = true;
	}
	SetRedraw();
}

#define COPYITEMTEXTLENGTH 35
void VaTree::AddCopy(const CStringW& buf)
{
	static CStringW lBuf;
	m_currentCopyBuffer = buf;
	if (!Psettings->m_clipboardCnt)
		return;
	if (lBuf == buf)
		return;
	lBuf = buf;
	if (lBuf.GetLength() > 100000 || !lBuf.GetLength())
		return;
	const CopyData* str = new CopyData(buf);
	token itemTxt = WideToMbcs(str->mData, str->mData.GetLength());
	itemTxt.ReplaceAll(TRegexp("[\t\r\n]"), string(""));
	itemTxt.ReplaceAll(TRegexp("^[ ]+"), string(""));
	CStringW tmp = WTString(itemTxt.c_str()).Wide();
	CStringW title;
	CStringW tmp2(tmp.Left(COPYITEMTEXTLENGTH));
	tmp2.Replace(L"&", L"&&");
	title += tmp2;
	if (tmp.GetLength() > COPYITEMTEXTLENGTH)
		title += L"...";
	if (tmp.IsEmpty())
		title = L"(whitespace)";

	SetRedraw(false);
	HTREEITEM sav, rec = InsertItemW(title, 7, 8, m_copybuf, m_insOrder);
	SetItemData(rec, (DWORD_PTR)str);
	sav = rec;
	// remove dupes
	while (rec)
	{
		rec = GetNextItem(rec, TVGN_NEXT);
		if (rec)
		{
			const CopyData* curData = (CopyData*)GetItemData(rec);
			// compare data, not item title since title is guaranteed to be unique
			if (curData->mHash == str->mHash && curData->mData.GetLength() == str->mData.GetLength())
			{
				// identical data, so delete
				DeleteCopyItem(rec);
				break;
			}
		}
	}
	rec = sav;
	// limit item count
	for (uint n = Psettings->m_clipboardCnt - 1; rec; n--)
	{
		rec = GetNextItem(rec, TVGN_NEXT);
		if (n < 1 && rec)
		{
			DeleteCopyItem(rec);
			break;
		}
	}
	if (!m_copyExp)
	{
		Expand(m_copybuf, TVE_EXPAND);
		m_copyExp = true;
	}
	SetRedraw();
}

void VaTree::OnRButtonDown(UINT nFlags, CPoint point)
{
	// CTreeCtrl::OnRButtonDown(nFlags, point);

	HTREEITEM item = HitTest(point);
	if (item)
		SelectItem(item);
	if (item && GetNextItem(item, TVGN_CHILD))
	{
		// TODO: right click menu
	}
}

LRESULT VaTree::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_DESTROY)
		return TRUE;
	try
	{
		LRESULT res = CTreeCtrl::DefWindowProc(message, wParam, lParam);
		return res;
	}
	catch (...)
	{
		VALOGEXCEPTION("VAT:");
		ASSERT(FALSE);
	}
	return 0;
}

void VaTree::Shutdown()
{
	DWORD n = Psettings->m_RecentCnt ? Psettings->m_RecentCnt - 1 : 0;
	HTREEITEM h;
	CStringW tmp;

	{
		SetReadWrite(METHODFILE);
		CFileW methout;
		methout.Open(METHODFILE, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit |
		                             CFile::typeBinary);
		h = GetNextItem(m_methods, TVGN_CHILD);
		while (h && n-- < Psettings->m_RecentCnt)
		{
			FilePos* fp = (FilePos*)GetItemData(h);
			CString__FormatW(tmp, L":%s|%s|%d\n", (const wchar_t*)GetItemTextW(h), (const wchar_t*)fp->m_file,
			                 fp->m_ln);
			methout.Write(tmp, tmp.GetLength() * sizeof(WCHAR));
			delete fp;
			h = GetNextItem(h, TVGN_NEXT);
		}
	}

	{
		SetReadWrite(FILEFILE);
		CFileW fileout;
		fileout.Open(FILEFILE, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit |
		                           CFile::typeBinary);
		n = Psettings->m_RecentCnt ? Psettings->m_RecentCnt - 1 : 0;
		h = GetNextItem(m_files, TVGN_CHILD);
		while (h && n-- < Psettings->m_RecentCnt)
		{
			FilePos* fp = (FilePos*)GetItemData(h);
			CString__FormatW(tmp, L"%s|%d\n", (const wchar_t*)fp->m_file, fp->m_ln);
			fileout.Write(tmp, tmp.GetLength() * sizeof(WCHAR));
			delete fp;
			h = GetNextItem(h, TVGN_NEXT);
		}
	}

	{
		SetReadWrite(COPYFILE);
		CFileW copyout;
		copyout.Open(COPYFILE, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::modeNoInherit |
		                           CFile::typeBinary);
		n = Psettings->m_clipboardCnt ? Psettings->m_clipboardCnt - 1 : 0;
		h = GetNextItem(m_copybuf, TVGN_CHILD);
		while (h && n-- < Psettings->m_clipboardCnt)
		{
			CopyData* fp = (CopyData*)GetItemData(h);
			CString__FormatW(tmp, L"%s\f", (const wchar_t*)fp->mData);
			copyout.Write(tmp, tmp.GetLength() * sizeof(WCHAR));
			delete fp;
			h = GetNextItem(h, TVGN_NEXT);
		}
	}

	{
		SetReadWrite(BKMKFILE);
		CFileW* bkmkout = nullptr;
		if (gShellAttr->SupportsBookmarks())
		{
			bkmkout = new CFileW;
			bkmkout->Open(BKMKFILE, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive |
			                            CFile::modeNoInherit | CFile::typeBinary);
		}
		h = GetNextItem(m_bookmarks, TVGN_CHILD);
		// no limit to number of bookmarks
		while (h)
		{
			FilePos* fp = (FilePos*)GetItemData(h);
			if (Psettings->m_keepBookmarks && bkmkout)
			{
				CString__FormatW(tmp, L"%s|%d\n", (const wchar_t*)fp->m_file, fp->m_ln);
				bkmkout->Write(tmp, tmp.GetLength() * sizeof(WCHAR));
			}
			delete fp;
			h = GetNextItem(h, TVGN_NEXT);
		}
		delete bkmkout;
	}
}
#include "workspacetab.h"
void VaTree::OnDestroy()
{
	Shutdown();
	CTreeCtrl::OnDestroy();
}

// TODO: wierd tooltip problem with undocked window

void VaTree::ToggleBookmark(const CStringW& file, int line, bool add /* = true*/)
{
	CStringW txt;
	CString__FormatW(txt, L"%s:%d", (const wchar_t*)Basename(file), line + 1);

	// remove bookmark or prevent double entry
	HTREEITEM last = GetNextItem(m_bookmarks, TVGN_CHILD);
	while (last)
	{
		if (!GetItemTextW(last).CompareNoCase(txt))
		{
			// if (!add) // TOGGLE OFF
			DeleteItem(last);
			return;
		}
		last = GetNextItem(last, UINT(m_insOrder == TVI_FIRST ? TVGN_NEXT : TVGN_PREVIOUS));
	}

	if (add)
	{
		last = InsertItemW(txt, 7, 8, m_bookmarks, TVI_SORT);
		FilePos* fp = new FilePos(file, line);
		SetItemData(last, (DWORD_PTR)fp);
		if (!m_bookmarkExp)
		{
			Expand(m_bookmarks, TVE_EXPAND);
			m_bookmarkExp = true;
		}
	}
}

HTREEITEM
VaTree::InsertItemW(const CStringW& lpszItem, int nImage, int nSelectedImage, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	ASSERT(::IsWindow(m_hWnd));
	return InsertItemW(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE, lpszItem, nImage, nSelectedImage, 0, 0, 0, hParent,
	                   hInsertAfter);
}

HTREEITEM
VaTree::InsertItemW(UINT nMask, const CStringW& lpszItem, int nImage, int nSelectedImage, UINT nState, UINT nStateMask,
                    LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	ASSERT(::IsWindow(m_hWnd));
	TVINSERTSTRUCTW tvis;
	tvis.hParent = hParent;
	tvis.hInsertAfter = hInsertAfter;
	tvis.item.mask = nMask;
	tvis.item.pszText = (LPWSTR)(LPCWSTR)lpszItem;
	tvis.item.iImage = nImage;
	tvis.item.iSelectedImage = nSelectedImage;
	tvis.item.state = nState;
	tvis.item.stateMask = nStateMask;
	tvis.item.lParam = lParam;
	return (HTREEITEM)::SendMessageW(m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

CStringW VaTree::GetItemTextW(HTREEITEM hItem) const
{
	ASSERT(::IsWindow(m_hWnd));
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
		::SendMessageW(m_hWnd, TVM_GETITEMW, 0, (LPARAM)&item);
		nRes = lstrlenW(item.pszText);
	} while (nRes >= nLen - 1);
	str.ReleaseBuffer();
	return str;
}

void VaTree::RemoveAllBookmarks(const CStringW& file)
{
	CStringW fileName(file);
	fileName = Basename(fileName);
	HTREEITEM last = GetNextItem(m_bookmarks, TVGN_CHILD);
	while (last)
	{
		CStringW itemTxt = GetItemTextW(last);
		int cutPos = itemTxt.Find(L':');
		itemTxt = itemTxt.Left(cutPos);
		int strcmpRes = itemTxt.CompareNoCase(fileName);
		HTREEITEM deleteItem = last;
		last = GetNextItem(last, TVGN_NEXT);
		if (!strcmpRes)
			DeleteItem(deleteItem);
		else if (strcmpRes > 0)
			return;
	}
}

void VaTree::SetFileBookmarks(const CStringW& filename, AttrLst* lst)
{
	HTREEITEM last = GetNextItem(m_bookmarks, TVGN_CHILD);
	while (last)
	{
		FilePos* pf = (FilePos*)GetItemData(last);
		if (!pf->m_file.CompareNoCase(filename))
			lst->Line(pf->m_ln)->ToggleBookmark();
		last = GetNextItem(last, TVGN_NEXT);
	}
}

void VaTree::ReadPasteHistory()
{
	CStringW fileContents;
	if (IsFile(COPYFILE))
		::ReadFileUtf16(COPYFILE, fileContents);
	LPWSTR pBegin = fileContents.GetBuffer(0);

	while (pBegin && *pBegin)
	{
		LPWSTR pEnd = (LPWSTR)wcschr(pBegin, L'\f');
		if (!pEnd)
			break;
		*pEnd++ = L'\0';
		if (wcslen(pBegin))
			AddCopy(pBegin);
		pBegin = pEnd;
	}
	m_currentCopyBuffer.Empty();
	fileContents.ReleaseBuffer(0);
}

void VaTree::Navigate(bool back /* = true */)
{
	HTREEITEM last = GetNextItem(m_methods, TVGN_CHILD);
	HTREEITEM item = nullptr, first = last;
	if (!g_curItem)
		g_curItem = last;
	while (last)
	{
		HTREEITEM next = GetNextItem(last, TVGN_NEXT);
		if (!back && g_curItem == next)
			item = last;
		if (back && g_curItem == last)
			item = next;
		last = next;
	}
	if (!item)
		item = back ? first : last;
	if (item)
	{
		g_curItem = item;
		FilePos* fp = (FilePos*)GetItemData(item);
		if (fp)
			DelayFileOpen(fp->m_file, fp->m_ln + 1, nullptr, TRUE);
	}
}
