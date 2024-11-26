#include "StdAfxEd.h"
#include "LiveOutlineFrame.h"
#include "resource.h"
#include "expansion.h"
#include "FileLineMarker.h"
#include "Oleobj.h"
#include "VAParse.h"
#include "UndoContext.h"
#include "VaService.h"
#include "RegKeys.h"
#include "Registry.h"
#include "PauseRedraw.h"
#include "VaMessages.h"
#include "Settings.h"
#include "DragDropTreeCtrl.h"
#include "FileTypes.h"
#include "..\VaPkg\VaPkgUI\PkgCmdID.h"
#include "MenuXP\Tools.h"
#include "VARefactor.h"
#include "DBLock.h"
#include "DevShellService.h"
#include "Directories.h"
#include "WindowUtils.h"
#include "RenameReferencesDlg.h"
#include "ParseThrd.h"
#include "FeatureSupport.h"
#include "project.h"
#include "tree_state.h"
#include "ImageListManager.h"
#include "VAAutomation.h"
#include "FILE.H"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// strings
#define ID_RK_OUTLINE_AUTOUPDATE "OutlineAutoUpdate"
#define ID_RK_OUTLINE_ALLOWDRAG "OutlineAllowDrag"
#define ID_RK_OUTLINE_FILTERFLAGS "OutlineFilterFlags"
#define ID_RK_OUTLINE_FILTERFLAGS_SAVED1 "OutlineFilterFlagsSaved1"
#define ID_RK_OUTLINE_FILTERFLAGS_SAVED2 "OutlineFilterFlagsSaved2"
#define ID_RK_OUTLINE_AUTOEXPAND "OutlineAutoExpand"
#define ID_RK_OUTLINE_STRIPSCOPE "OutlineStripScopeFromGroupedMethods"
#define ID_RK_OUTLINE_DISPLAYTOOLTIPS "OutlineDisplayTooltips"

// messages
#define WM_VA_INSERTITEMS WM_USER + 20

// command ids
#define ID_TOGGLE_AUTOUPDATE 0xE100 // tracking ids need to be in contiguous range
#define ID_EDIT_GOTO 0xE12D
#define ID_EDIT_SELECT 0xE12E
#define ID_FILTER_TOGGLE_INCLUDES 0xE12F
#define ID_FILTER_TOGGLE_COMMENTS 0xE130
#define ID_FILTER_TOGGLE_GLOBALS 0xE131
#define ID_FILTER_TOGGLE_TAGS 0xE132
#define ID_FILTER_TOGGLE_PREPROCESSOR 0xE133
#define ID_FILTER_TOGGLE_METHODS 0xE134
#define ID_FILTER_TOGGLE_MEMBERS 0xE135
#define ID_FILTER_TOGGLE_MACROS 0xE136
#define ID_FILTER_TOGGLE_REGIONS 0xE137
#define ID_FILTER_TOGGLE_ENUMS 0xE138
#define ID_FILTER_TOGGLE_TYPES 0xE139
#define ID_FILTER_TOGGLE_NAMESPACES 0xE13A
#define ID_FILTER_TOGGLE_GROUPS 0xE13B
#define ID_FILTER_TOGGLE_MSGMAP 0xE13C
// ...
#define ID_LAYOUT_TREE 0xE150
#define ID_LAYOUT_LIST 0xE151
#define ID_FILTER_ALL 0xE154
#define ID_FILTER_NONE 0xE155
#define ID_FILTER_SAVEAS1 0xE156
#define ID_FILTER_SAVEAS2 0xE157
#define ID_FILTER_LOAD1 0xE158
#define ID_FILTER_LOAD2 0xE159
#define ID_REFRESH_NOW 0xE15A
#define ID_ALLOW_DRAGGING 0xE15B
#define ID_REFACTOR_RENAME 0xE15C
#define ID_REFACTOR_FINDREFS 0xE15D
#define ID_REFACTOR_ADDMEMBER 0xE15E
#define ID_REFACTOR_ADDSIMILARMEMBER 0xE15F
#define ID_REFACTOR_CHANGESIG 0xE160
#define ID_SURROUND_COMMENT_C 0xE161
#define ID_SURROUND_UNCOMMENT_C 0xE162
#define ID_SURROUND_COMMENT_CPP 0xE163
#define ID_SURROUND_UNCOMMENT_CPP 0xE164
#define ID_SURROUND_REGION 0xE165
#define ID_SURROUND_IFDEF 0xE166
#define ID_SURROUND_REFORMAT 0xE167
#define ID_LOG_OUTLINE 0xE170
#define ID_TOGGLE_ALLOWDRAG 0xE171
#define ID_TOGGLE_AUTOEXPAND 0xE172
#define ID_REFACTOR_CREATEIMPLEMENTATION 0xE173
#define ID_REFACTOR_CREATEDECLARATION 0xE174
#define ID_EXPAND_ALL 0xE175
#define ID_COPY_ALL 0xE176
#define ID_COLLAPSE_ALL 0xE177
#define ID_SURROUND_NAMESPACE 0xE178
#define ID_TOGGLE_DISPLAYTOOLTIPS 0xE179

const DWORD kDefaultFlags = 0xffffffff & ~FileOutlineFlags::ff_Comments;
const UINT_PTR kRefreshTimerId = 2100;
const int kMaxNodesToSupportTreeStateMemory = 8000;

static bool PopulateRefactorMenu(CMenu& menu, DType* sym, DType* context);

LiveOutlineFrame::LiveOutlineFrame(HWND hWndParent)
    : VADialog(IDD_FILEOUTLINE, CWnd::FromHandle(hWndParent), fdAll, flAntiFlicker | flNoSavePos), mTree(NULL),
      mParent(gShellAttr->IsMsdevOrCppBuilder() ? NULL : hWndParent), mEdModCookie(0), mLayoutAsList(false), mRefreshTimerId(0),
      mInTransitionPeriod(false), mUpdateIsPending(false), mEdModCookieAtDragStart(0), mSelectItemDuringRefresh(false),
      mAutoExpand(false), mStripScopeFromGrpdMthds(true)
{
	mAutoUpdate = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_AUTOUPDATE, true);
	mAllowDragging = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_ALLOWDRAG, true);
	mDisplayTooltips = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_DISPLAYTOOLTIPS, true);
	mAutoExpand = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_AUTOEXPAND, false);
	mStripScopeFromGrpdMthds = ::GetRegBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_STRIPSCOPE, true);
	mFilterFlags = ::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS, kDefaultFlags);
#ifdef _DEBUG
	mFilterFlagsSaved1 = ::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS_SAVED1, kDefaultFlags);
	mFilterFlagsSaved2 = ::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS_SAVED2, kDefaultFlags);
#else
	mFilterFlagsSaved1 = mFilterFlagsSaved2 = kDefaultFlags;
#endif // _DEBUG

	Create(IDD_FILEOUTLINE, CWnd::FromHandle(hWndParent));
	SetWindowText("VA Outline"); // needed for vc6 tab
}

LiveOutlineFrame::~LiveOutlineFrame()
{
	::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_AUTOUPDATE, mAutoUpdate);
	::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_ALLOWDRAG, mAllowDragging);
	::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_DISPLAYTOOLTIPS, mDisplayTooltips);
	::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_AUTOEXPAND, mAutoExpand);
	::SetRegValueBool(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_STRIPSCOPE, mStripScopeFromGrpdMthds);
	::SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS, (DWORD)mFilterFlags);

	ClearWithoutRedraw();
	if (m_hWnd && ::IsWindow(m_hWnd))
		DestroyWindow();

	delete mTree;
}

#pragma warning(push)
#pragma warning(disable : 4191)
BEGIN_MESSAGE_MAP(LiveOutlineFrame, VADialog)
//{{AFX_MSG_MAP(LiveOutlineFrame)
ON_MENUXP_MESSAGES()
ON_WM_CONTEXTMENU()
ON_WM_TIMER()
ON_WM_ERASEBKGND()
ON_MESSAGE(WM_VA_INSERTITEMS, OnInsert)
ON_NOTIFY(NM_DBLCLK, IDC_TREE1, OnDoubleClickTree)
ON_NOTIFY(NM_RETURN, IDC_TREE1, OnDoubleClickTree)
ON_NOTIFY(NM_RCLICK, IDC_TREE1, OnRightClickTree)
ON_COMMAND(ID_EDIT_COPY, OnCopy)
ON_COMMAND(ID_EDIT_CUT, OnCut)
ON_COMMAND(ID_EDIT_PASTE, OnPaste)
ON_COMMAND(ID_EDIT_CLEAR, OnDelete)
ON_COMMAND(ID_EDIT_GOTO, OnGoto)
ON_COMMAND(ID_EDIT_SELECT, OnSelectItemInEditor)
ON_COMMAND(ID_TOGGLE_AUTOUPDATE, OnToggleAutoupdate)
ON_COMMAND(ID_TOGGLE_ALLOWDRAG, OnToggleAllowDrag)
ON_COMMAND(ID_TOGGLE_AUTOEXPAND, OnToggleAutoExpand)
ON_COMMAND(ID_FILTER_ALL, OnFilterAll)
ON_COMMAND(ID_FILTER_NONE, OnFilterNone)
ON_COMMAND(ID_FILTER_TOGGLE_INCLUDES, OnFilterToggleIncludes)
ON_COMMAND(ID_FILTER_TOGGLE_COMMENTS, OnFilterToggleComments)
ON_COMMAND(ID_FILTER_TOGGLE_GLOBALS, OnFilterToggleGlobals)
ON_COMMAND(ID_FILTER_TOGGLE_TAGS, OnFilterToggleTags)
ON_COMMAND(ID_FILTER_TOGGLE_PREPROCESSOR, OnFilterToggleIfdefs)
ON_COMMAND(ID_FILTER_TOGGLE_METHODS, OnFilterToggleFunctions)
ON_COMMAND(ID_FILTER_TOGGLE_MEMBERS, OnFilterToggleVariables)
ON_COMMAND(ID_FILTER_TOGGLE_MACROS, OnFilterToggleMacros)
ON_COMMAND(ID_FILTER_TOGGLE_REGIONS, OnFilterToggleRegions)
ON_COMMAND(ID_FILTER_TOGGLE_ENUMS, OnFilterToggleEnums)
ON_COMMAND(ID_FILTER_TOGGLE_MSGMAP, OnFilterToggleMsgMap)
ON_COMMAND(ID_FILTER_TOGGLE_TYPES, OnFilterToggleTypes)
ON_COMMAND(ID_FILTER_TOGGLE_NAMESPACES, OnFilterToggleNamespaces)
ON_COMMAND(ID_FILTER_TOGGLE_GROUPS, OnFilterToggleGroups)
ON_COMMAND(ID_LAYOUT_LIST, OnSelectLayoutList)
ON_COMMAND(ID_LAYOUT_TREE, OnSelectLayoutTree)
ON_COMMAND(ID_FILTER_SAVEAS1, OnSaveAsFilter1)
ON_COMMAND(ID_FILTER_SAVEAS2, OnSaveAsFilter2)
ON_COMMAND(ID_FILTER_LOAD1, OnLoadFilter1)
ON_COMMAND(ID_FILTER_LOAD2, OnLoadFilter2)
ON_COMMAND(ID_REFRESH_NOW, OnRefreshNow)
ON_COMMAND(ID_REFACTOR_RENAME, OnRefactorRename)
ON_COMMAND(ID_REFACTOR_FINDREFS, OnRefactorFindRefs)
ON_COMMAND(ID_REFACTOR_ADDMEMBER, OnRefactorAddMember)
ON_COMMAND(ID_REFACTOR_ADDSIMILARMEMBER, OnRefactorAddSimilarMember)
ON_COMMAND(ID_REFACTOR_CHANGESIG, OnRefactorChangeSignature)
ON_COMMAND(ID_REFACTOR_CREATEIMPLEMENTATION, OnRefactorCreateImplementation)
ON_COMMAND(ID_REFACTOR_CREATEDECLARATION, OnRefactorCreateDeclaration)
ON_COMMAND(ID_SURROUND_COMMENT_C, OnSurroundCommentC)
ON_COMMAND(ID_SURROUND_UNCOMMENT_C, OnSurroundUncommentC)
ON_COMMAND(ID_SURROUND_COMMENT_CPP, OnSurroundCommentCPP)
ON_COMMAND(ID_SURROUND_UNCOMMENT_CPP, OnSurroundUncommentCPP)
ON_COMMAND(ID_SURROUND_REGION, OnSurroundRegion)
ON_COMMAND(ID_SURROUND_NAMESPACE, OnSurroundNamespace)
ON_COMMAND(ID_SURROUND_IFDEF, OnSurroundIfdef)
ON_COMMAND(ID_SURROUND_REFORMAT, OnSurroundReformat)
ON_COMMAND(ID_LOG_OUTLINE, OnLogOutline)
ON_COMMAND(ID_EXPAND_ALL, OnExpandAll)
ON_COMMAND(ID_COLLAPSE_ALL, OnCollapseAll)
ON_COMMAND(ID_COPY_ALL, OnCopyAll)
ON_COMMAND(ID_TOGGLE_DISPLAYTOOLTIPS, OnToggleDisplayTooltips)

//}}AFX_MSG_MAP
END_MESSAGE_MAP()

IMPLEMENT_MENUXP(LiveOutlineFrame, VADialog);
#pragma warning(pop)

void LiveOutlineFrame::IdePaneActivated()
{
	if (mTree && GetFocus() != mTree)
		mTree->SetFocus();

	if (g_currentEdCnt)
		Refresh(WTString(), g_currentEdCnt);
	else
	{
		mEdcnt = NULL;
		Clear();
	}
}

BOOL LiveOutlineFrame::OnInitDialog()
{
	__super::OnInitDialog();

	CTreeCtrl* pTree = (CTreeCtrl*)GetDlgItem(IDC_TREE1);
	mTree = new DragDropTreeCtrl(this);
	mTree->SubclassWindow(pTree->m_hWnd);

	gImgListMgr->SetImgListForDPI(*mTree, ImageListManager::bgTree, TVSIL_NORMAL);
	mTree->SetFontType(VaFontType::EnvironmentFont);

	DWORD stylesToAdd = TVS_NONEVENHEIGHT | TVS_HASBUTTONS | TVS_HASLINES | TVS_SHOWSELALWAYS /*| TVS_INFOTIP*/ |
	                    WS_TABSTOP | (mTree->IsVS2010ColouringActive() ? TVS_LINESATROOT : 0u);
	DWORD stylesToRemove = TVS_INFOTIP;
	if (CVS2010Colours::IsExtendedThemeActive())
		stylesToAdd |= TVS_FULLROWSELECT;
	else
		stylesToRemove |= TVS_FULLROWSELECT;
	mTree->ModifyStyle(stylesToRemove, stylesToAdd);
	mTree->EnableToolTips();

	ModifyStyle(DS_3DLOOK, 0);
	mTree->ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
	if (gShellAttr->IsMsdev())
	{
		// WS_EX_CLIENTEDGE leaves a bold edge on the left that is
		// more noticeable than the soft edge on the bottom and right
		// with WS_EX_STATICEDGE.
		mTree->ModifyStyleEx(0, WS_EX_STATICEDGE);

		// case=9976
		CToolTipCtrl* treeTips = mTree->GetToolTips();
		if (treeTips)
			treeTips->SetWindowPos(&wndTopMost, 0, 0, 10, 10, SWP_NOACTIVATE);
	}
	else if (::GetWinVersion() < wvWinXP)
	{
		// vs200x in win2000
		mTree->ModifyStyle(0, WS_BORDER);
	}

	CRect rc;
	GetParent()->GetWindowRect(&rc);
	GetParent()->ScreenToClient(&rc);
	MoveWindow(&rc);

	AddSzControl(IDC_TREE1, mdResize, mdResize);

	return TRUE;
}

void LiveOutlineFrame::InsertToTree(HTREEITEM parentItem, LineMarkers::Node& node, bool nestedDisplay,
                                    UINT expandToLine, HTREEITEM* bold_this_item, HTREEITEM* select_this_item)
{
	_ASSERTE(bold_this_item);
	_ASSERTE(select_this_item);

	UINT expandFlags = TVIS_EXPANDED;

	CStringW itemTxt;
	for (size_t idx = 0; idx < node.GetChildCount(); ++idx)
	{
		LineMarkers::Node& ch = node.GetChild(idx);
		FileLineMarker& mkr = *ch;

		UINT state = 0;

		FileOutlineFlags::DisplayFlag displayFlag = (FileOutlineFlags::DisplayFlag)mkr.mDisplayFlag;

		if (TVI_ROOT == parentItem && node.GetChildCount() == 1 && ch.GetChildCount())
			state |= expandFlags;

		switch (displayFlag)
		{
		case FileOutlineFlags::ff_Hidden:
			continue;
			break;

		case FileOutlineFlags::ff_IncludePseudoGroup:
		case FileOutlineFlags::ff_MacrosPseudoGroup:
		case FileOutlineFlags::ff_FwdDeclPseudoGroup:
		case FileOutlineFlags::ff_GlobalsPseudoGroup:
			// don't expand by default
			if (ch.GetChildCount() == 1)
			{
				// only one entry, so skip pseudo group
				InsertToTree(parentItem, ch, nestedDisplay, expandToLine, bold_this_item, select_this_item);
				continue;
			}
			break;

		case FileOutlineFlags::ff_Preprocessor:
			// don't expand preprocs by default, unless the block contains more than 10 lines
			if (mkr.mEndLine != -1 && (mkr.mEndLine - mkr.mStartLine) > 10 && ch.GetChildCount())
				state |= expandFlags;
			// don't expand preprocs by default, unless is a child of the root that has fewer than 4 children
			else if (TVI_ROOT == parentItem && node.GetChildCount() < 4 && ch.GetChildCount())
				state |= expandFlags;
			break;

		case FileOutlineFlags::ff_MethodsPseudoGroup:
			state |= expandFlags;
			if (ch.GetChildCount() == 1)
			{
				// only one entry, so skip pseudo group
				InsertToTree(parentItem, ch, nestedDisplay, expandToLine, bold_this_item, select_this_item);
				continue;
			}
			else
			{
				if (mStripScopeFromGrpdMthds)
				{
					// strip out class/namespace scope from child nodes.
					// Watch out for:
					// A::B::C(std::string str)
					//
					for (size_t chIdx = 0; chIdx < ch.GetChildCount(); ++chIdx)
					{
						const CStringW strFull = ch.GetChild(chIdx).Contents().mText;
						CStringW str = strFull;
						int posOpenBrace = str.Find(L'(');
						if (posOpenBrace > 0)
							str = str.Left(posOpenBrace);

						int posName = str.Find(L"::");
						if (posName > 0)
						{
							for (;;)
							{
								int posName2 = str.Find(L"::", posName + 2);
								if (posName2 > 0)
									posName = posName2;
								else
									break;
							}

							ch.GetChild(chIdx).Contents().mText = strFull.Mid(posName + 2);
						}
					}
				}
			}
			break;

		case FileOutlineFlags::ff_Comments:
			if (!mFilterFlags.IsEnabled(displayFlag))
			{
				// if next item is not comment, then attach this comment to it.
				if (idx + 1 < node.GetChildCount())
				{
					LineMarkers::Node& nextCh = node.GetChild(idx + 1);
					FileLineMarker& nextMkr = *nextCh;

					if (nextMkr.mDisplayFlag != FileOutlineFlags::ff_Comments)
					{
						nextMkr.mStartLine = mkr.mStartLine;
						nextMkr.mStartCp = mkr.mStartCp;

						// if next item is a pseudo-group, then set beginning of
						// first child to include comment also.
						if (nextCh.Contents().mDisplayFlag & FileOutlineFlags::ff_PseudoGroups &&
						    nextCh.GetChildCount() > 0)
						{
							nextCh.GetChild(0).Contents().mStartLine = mkr.mStartLine;
							nextCh.GetChild(0).Contents().mStartCp = mkr.mStartCp;
						}
						continue;
					}
				}
			}
			break;

		case FileOutlineFlags::ff_None:
			switch (mkr.mType)
			{
			case CLASS:
			case STRUCT:
			case NAMESPACE: // [case: 27730] fix for multi-depth namespaces
				state |= expandFlags;
				break;

			default:
				break;
			}
			break;

		case FileOutlineFlags::ff_Expanded:
		case FileOutlineFlags::ff_TagsAndLabels:
		case FileOutlineFlags::ff_Enums:
			state |= expandFlags;
			break;

		case FileOutlineFlags::ff_MessageMaps:
		case FileOutlineFlags::ff_TypesAndClasses:
		case FileOutlineFlags::ff_Includes:
		case FileOutlineFlags::ff_Globals:
		case FileOutlineFlags::ff_MethodsAndFunctions:
		case FileOutlineFlags::ff_MembersAndVariables:
		case FileOutlineFlags::ff_Macros:
		case FileOutlineFlags::ff_Regions:
		case FileOutlineFlags::ff_Namespaces:
		case FileOutlineFlags::ff_FwdDecl:
		default:
			// only expand if node has children and expandToLine is within the node.
			if (ch.GetChildCount() > 0 && ch.Contents().mStartLine <= (int)expandToLine &&
			    ch.Contents().mEndLine >= (int)expandToLine)
			{
				state |= expandFlags;
			}
			break;
		}

		HTREEITEM it;
		if (0 == displayFlag || mFilterFlags.IsEnabled(displayFlag))
		{
			itemTxt = mkr.mText;
			itemTxt.Replace(L'\t', L' ');
			int iconIdx = mkr.GetIconIdx();
			it = mTree->InsertItemW(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_PARAM, itemTxt,
			                        iconIdx, iconIdx, state, state, (LPARAM)&mkr, parentItem, TVI_LAST);

			bool parentIsExpanded = parentItem && parentItem != TVI_ROOT
			                            ? (mTree->GetItemState(parentItem, 0xffffffff) & TVIS_EXPANDED) != 0
			                            : true;

			// if this node is expanded, make sure parent is too
			if ((TVIS_EXPANDED & state) && !parentIsExpanded)
			{
				mTree->SetItemState(parentItem, TVIS_EXPANDED, 0xffffffff);
				parentIsExpanded = true;
			}

			if ((ch.Contents().mStartLine <= (int)expandToLine && ch.Contents().mEndLine >= (int)expandToLine))
			{
				HTREEITEM curItem = NULL;
				if (parentIsExpanded)
					curItem = it;
				else if (parentItem && parentItem != TVI_ROOT)
					curItem = parentItem;

				if (curItem)
				{
					*bold_this_item = curItem;

					if (mSelectItemDuringRefresh)
					{
						// [case 8077] select the current item after refresh when modifying the file
						if (*bold_this_item && *bold_this_item != TVI_ROOT)
						{
							*select_this_item = *bold_this_item;
						}
					}
				}
			}
		}
		else
		{
			it = parentItem;
		}

		if (ch.GetChildCount())
			InsertToTree(nestedDisplay ? it : parentItem, ch, nestedDisplay, expandToLine, bold_this_item,
			             select_this_item);
	}
}

static HTREEITEM GetLastUnexpandedParent(CTreeCtrl& tree, HTREEITEM item)
{
	if (!item)
		return NULL;

	std::list<HTREEITEM> parents;
	do
	{
		parents.push_front(item);
		item = tree.GetParentItem(item);
	} while (item && (item != tree.GetRootItem()));

	std::list<HTREEITEM>::const_iterator it;
	for (it = parents.begin(); it != parents.end(); ++it)
	{
		if (!(tree.GetItemState(*it, TVIS_EXPANDED) & TVIS_EXPANDED))
			break; // stop at first collapsed parent
	}
	if (it == parents.end())
		it = --parents.end();

	return *it;
}

void LiveOutlineFrame::Insert(LineMarkers* pMarkers)
{
	HTREEITEM bold_this_item = NULL;

	{
		PauseRedraw pr(mTree, false);
		ClearWithoutRedraw();
		{
			AutoLockCs l(mMarkersLock);
			mMarkers.reset(pMarkers);
		}

		if (g_currentEdCnt && mEdcnt == g_currentEdCnt && pMarkers)
		{
			SHORT old_height = mTree->GetItemHeight();

			switch (mEdcnt->m_ftype)
			{
			case XML:
			case XAML:
				// no icons in tree
				mTree->SetImageList(NULL, TVSIL_NORMAL);
				break;

			default:
				gImgListMgr->SetImgListForDPI(*mTree, ImageListManager::bgTree, TVSIL_NORMAL);
				break;
			}

			if (old_height != mTree->GetItemHeight())
				mTree->SetItemHeight(old_height); // case 20889 workaround

			HTREEITEM select_this_item = NULL;
			InsertToTree(TVI_ROOT, pMarkers->Root(), !mLayoutAsList, (UINT)mEdcnt->CurLine(), &bold_this_item,
			             &select_this_item);

			_ASSERTE(!mEdcnt_last_refresh);
			mEdcnt_last_refresh = mEdcnt;
			if (!mEdcnt_last_refresh->m_vaOutlineTreeState)
				mEdcnt_last_refresh->m_vaOutlineTreeState = new tree_state::storage();
			if (!mAutoExpand && mTree->GetCount() < kMaxNodesToSupportTreeStateMemory)
				tree_state::traverse(*mTree, tree_state::restore(*mEdcnt_last_refresh->m_vaOutlineTreeState));

			if (select_this_item)
				mTree->Select(mAutoExpand ? select_this_item : GetLastUnexpandedParent(*mTree, select_this_item),
				              TVGN_CARET);

			//			OutputDebugString("FEC:    ---- restore  ----");
			//			for(tree_state::storage::const_iterator it = mEdcnt_last_refresh->m_vaOutlineTreeState->begin();
			// it
			//!= mEdcnt_last_refresh->m_vaOutlineTreeState->end(); ++it) { 				char temp[512];
			//!sprintf(temp, "FEC: %c %s",
			// it->second ? '+' : '-', it->first.c_str()); 				OutputDebugString(temp);
			//			}
		}
		mTree->SetScrollPos(SB_HORZ, 0);
	}

	_ASSERTE(!mLastBolded);

	if (bold_this_item)
	{
		// can't combine this PauseRedraw with the one above (EnsureVisible won't)
		PauseRedraw pr(mTree, false);

		if (bold_this_item)
		{
			mTree->SetItemState(bold_this_item, TVIS_BOLD, TVIS_BOLD);
			mLastBolded = bold_this_item;
		}

		mTree->EnsureVisible(mAutoExpand ? mLastBolded : GetLastUnexpandedParent(*mTree, mLastBolded));
		mTree->SetScrollPos(SB_HORZ, 0);
	}

	mTree->RedrawWindow();
}

void LiveOutlineFrame::Clear()
{
	PauseRedraw pr(mTree, true);
	ClearWithoutRedraw();
}

void LiveOutlineFrame::ClearWithoutRedraw()
{
	if (mEdcnt_last_refresh)
	{
		if (!mEdcnt_last_refresh->m_vaOutlineTreeState)
			mEdcnt_last_refresh->m_vaOutlineTreeState = new tree_state::storage();
		mEdcnt_last_refresh->m_vaOutlineTreeState->clear();
		if (!mAutoExpand && mTree->GetCount() < kMaxNodesToSupportTreeStateMemory)
		{
			tree_state::traverse(*mTree, tree_state::save(*mEdcnt_last_refresh->m_vaOutlineTreeState));

			//			OutputDebugString("FEC:    ---- save  ----");
			//			for(tree_state::storage::const_iterator it = mEdcnt_last_refresh->m_vaOutlineTreeState->begin();
			// it
			//!= mEdcnt_last_refresh->m_vaOutlineTreeState->end(); ++it) { 				char temp[512];
			//!sprintf(temp, "FEC: %c %s",
			// it->second ? '+' : '-', it->first.c_str()); 				OutputDebugString(temp);
			//			}
		}

		mEdcnt_last_refresh = NULL;
	}

	mTree->PopTooltip();
	mTree->DeleteAllItems();
	mLastBolded = NULL;

	AutoLockCs l(mMarkersLock);
	mMarkers = NULL;
}

LRESULT
LiveOutlineFrame::OnInsert(WPARAM wp, LPARAM lp)
{
	Insert((LineMarkers*)wp);
	return TRUE;
}

void LiveOutlineFrame::OnToggleDisplayTooltips()
{
	mDisplayTooltips = !mDisplayTooltips;
	mTree->EnableToolTips(mDisplayTooltips);
}

BOOL LiveOutlineFrame::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	if ((GetFocus() == mTree || GetFocus() == this) && mTree && !mTree->IsDragging())
	{
		NMHDR* pNMHDR = (NMHDR*)lParam;

		if (pNMHDR->code == TVN_GETINFOTIPA)
		{
			LPNMTVGETINFOTIP lpGetInfoTipw = (LPNMTVGETINFOTIP)lParam;
			HTREEITEM i = lpGetInfoTipw->hItem;
			const WTString txt(GetTooltipText(i));

			for (int n = 0; n < lpGetInfoTipw->cchTextMax && txt[n]; n++)
			{
				lpGetInfoTipw->pszText[n] = txt[n];
				lpGetInfoTipw->pszText[n + 1] = '\0';
			}
			if (mTree)
				mTree->EnableToolTips(mDisplayTooltips); // it doesn't work properly when this is put in OnInitDialog(),
				                                         // it gets in a messed up state
			return TRUE;
		}

		if (pNMHDR->code == TVN_GETINFOTIPW)
		{
			LPNMTVGETINFOTIPW lpGetInfoTipw = (LPNMTVGETINFOTIPW)lParam;
			HTREEITEM i = lpGetInfoTipw->hItem;
			const WTString txt(GetTooltipText(i));
			int len = MultiByteToWideChar(CP_UTF8, 0, txt.c_str(), txt.GetLength(), lpGetInfoTipw->pszText,
			                              lpGetInfoTipw->cchTextMax);
			lpGetInfoTipw->pszText[len] = L'\0';
			if (mTree)
				mTree->EnableToolTips(mDisplayTooltips); // it doesn't work properly when this is put in OnInitDialog(),
				                                         // it gets in a messed up state
			return TRUE;
		}
	}

	return __super::OnNotify(wParam, lParam, pResult);
}

static WTString GetLines(EdCntPtr ed, int startLine, int endLine)
{
	WTString retval;
	const WTString ftxt(ed->GetBuf());
	const WTString eol(ed->GetLineBreakString());

	WTString tmp;
	int previousPos = 0;
	int curLinePos = 0;
	int lineCnt = 0;
	bool addedEllipsis = false;
	const bool kUseAllLines = (endLine - startLine) <= 12;

	_ASSERTE(endLine >= startLine);
	while (lineCnt < endLine)
	{
		curLinePos = ftxt.Find(eol, previousPos);
		if (curLinePos == -1)
			break;
		++lineCnt;
		if (lineCnt >= startLine && lineCnt < endLine)
		{
			if (kUseAllLines || lineCnt < (startLine + 5) || lineCnt > (endLine - 6))
			{
				tmp.WTFormat("%d:\t", lineCnt);
				retval += tmp;
				retval += ftxt.Mid(previousPos, curLinePos - previousPos);
				retval += "\r\n";
			}
			else if (!addedEllipsis)
			{
				addedEllipsis = true;
				retval += "...\r\n";
			}
		}
		previousPos = curLinePos + eol.GetLength();
	}

	return retval;
}

WTString LiveOutlineFrame::GetTooltipText(HTREEITEM item) const
{
	WTString tipTxt;
	if (!item || !mEdcnt)
		return tipTxt;

	const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(item);
	if (mkr)
	{
		int endLine = (int)mkr->mEndLine;
		if (endLine == -1 || endLine == (int)mkr->mStartLine)
			endLine = (int)mkr->mStartLine + 1;

		tipTxt = ::GetLines(mEdcnt, (int)mkr->mStartLine, endLine);
		tipTxt.TrimRight();
		tipTxt.Replace("\t", "    ");
	}

	return tipTxt;
}

void LiveOutlineFrame::OnDoubleClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 1;
	OnGoto();
}

void LiveOutlineFrame::SelectLinesInEditor(int startLine, int endLine) const
{
	_ASSERTE(g_currentEdCnt && mEdcnt == g_currentEdCnt);
	long startPos = mEdcnt->LineIndex(startLine);
	long endPos = endLine == -1 ? startPos : mEdcnt->LineIndex(endLine);
	mEdcnt->SetSel(startPos, endPos);
}

void LiveOutlineFrame::SelectCharsInEditor(int startCp, int endCp) const
{
	_ASSERTE(g_currentEdCnt && mEdcnt == g_currentEdCnt);
	mEdcnt->SetSel(startCp, endCp);
}

void LiveOutlineFrame::MoveToEditorLine(int line) const
{
	SelectLinesInEditor(line, line);
}

void LiveOutlineFrame::MoveToEditorCp(int cp) const
{
	SelectCharsInEditor(cp, cp);
}

void LiveOutlineFrame::OnRightClickTree(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0; // let the tree send us a WM_CONTEXTMENU msg

	const CPoint pt(GetCurrentMessage()->pt);
	HTREEITEM it = GetItemFromPos(pt);
	if (it)
		mTree->SelectItem(it);
}

void LiveOutlineFrame::OnGoto()
{
	HTREEITEM selItem = mTree->GetSelectedItem();
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !selItem)
		return;

	const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
	DelayFileOpen(mEdcnt->FileName(), (int)mkr->mGotoLine);
	MoveToEditorLine((int)mkr->mGotoLine);
}

void LiveOutlineFrame::OnCopy()
{
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !mTree->GetSelectedItemCount())
		return;

	CStringW txt = GetSelectedItemsText(false);
	::SaveToClipboard(mEdcnt->GetSafeHwnd(), txt);

	HTREEITEM selItem = mTree->GetSelectedItem();
	const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
	MoveToEditorLine((int)mkr->mStartLine);
	gShellSvc->GotoVaOutline();
}

void LiveOutlineFrame::OnCut()
{
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !mTree->GetSelectedItemCount())
		return;

	UndoContext uc("Cut");
	CStringW txt = GetSelectedItemsText(true);
	::SaveToClipboard(mEdcnt->GetSafeHwnd(), txt);
	OnModificationComplete();
}

void LiveOutlineFrame::OnPaste()
{
	HTREEITEM selItem = mTree->GetSelectedItem(); // ok for multi-select
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !selItem)
		return;

	const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
	MoveToEditorLine((int)mkr->mStartLine);

	UndoContext uc("Paste");
	const CStringW clipTxt(::GetClipboardText(m_hWnd));
	mEdcnt->InsertW(clipTxt, true, IsFeatureSupported(Feature_FormatAfterPaste) ? vsfAutoFormat : noFormat, false);
	MoveToEditorLine((int)mkr->mStartLine);
	OnModificationComplete();
}

void LiveOutlineFrame::OnDelete()
{
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !mTree->GetSelectedItemCount())
		return;

	UndoContext uc("Delete");
	GetSelectedItemsText(true); // ignore return
	OnModificationComplete();
}

void LiveOutlineFrame::OnSelectItemInEditor()
{
	HTREEITEM selItem = mTree->GetSelectedItem();
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !selItem)
		return;

	const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
	DelayFileOpen(mEdcnt->FileName(), (int)mkr->mStartLine);
	_ASSERTE(mkr->mEndCp > mkr->mStartCp);
	SelectCharsInEditor((int)mkr->mEndCp, (int)mkr->mStartCp);
}

// Refresh can be called from the UI thread and from the parser thread.
// If called from the parser thread, the fileText will be passed (populated
// from UI thread when the parse workjob was created).
// If called from the UI thread, we can get the text directly.
void LiveOutlineFrame::Refresh(WTString fileText, EdCntPtr ed, int edModCookie /*= -1*/)
{
	const HWND thisWnd = m_hWnd;

	if (!g_currentEdCnt || g_currentEdCnt != ed || !Psettings->m_enableVA || gShellIsUnloading)
	{
		mEdcnt = NULL;
		Clear();
		return;
	}

	// don't deref ed before the previous checks
	const HWND edWnd = ed->GetSafeHwnd();

	if (!::IsWindow(edWnd) || !IsFeatureSupported(Feature_Outline, ed->m_ScopeLangType))
	{
		mEdcnt = NULL;
		Clear();
		return;
	}

	if (edModCookie == -1)
	{
		_ASSERTE(g_mainThread == ::GetCurrentThreadId());
		_ASSERTE(fileText.IsEmpty());
		_ASSERTE(ed);
		edModCookie = ed->m_modCookie;
	}

	if (mEdcnt == ed && mEdModCookie == edModCookie && CTer::BUF_STATE_WRONG != mEdcnt->m_bufState)
	{
		AutoLockCs l(mMarkersLock);
		if (mMarkers.get() && mMarkers->Root().GetChildCount())
			return;
	}

	if (mEdcnt == ed)
		mInTransitionPeriod = false;
	else
	{
		mInTransitionPeriod = true;
		mEdcnt = ed;
	}
	mUpdateIsPending = true;

	LineMarkers* markers = new LineMarkers;
	if (fileText.IsEmpty() && g_mainThread == ::GetCurrentThreadId())
	{
		fileText = ed->GetBuf(TRUE); // GetBuf can't be forced from our thread...
		edModCookie = ed->m_modCookie;
	}

	markers->mModCookie = mEdModCookie = edModCookie;

	// what is this stuff?
	const WTString padding = "\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n";
	if (fileText.EndsWith(padding))
		fileText = fileText.Mid(0, fileText.GetLength() - padding.GetLength());

	MultiParsePtr mparse = ed->GetParseDb();
	if (!::GetFileOutline(fileText, *markers, mparse))
	{
		// [case: 87527] timed out, invalidate cache for retry next time
		markers->mModCookie = mEdModCookie = 0;
	}

	if (::IsWindow(thisWnd) && !gShellIsUnloading && ::IsWindow(edWnd))
	{
		// insert to tree from the UI thread instead of the parse thread
		// since inserting involves a sendmessage for each tree item
		SendMessage(WM_VA_INSERTITEMS, (WPARAM)markers);
	}
	else
	{
		// parsing during shutdown?
		delete markers;
	}
	mInTransitionPeriod = false;
	mUpdateIsPending = false;
}

void LiveOutlineFrame::OnToggleAutoupdate()
{
	mAutoUpdate = !mAutoUpdate;
}

void LiveOutlineFrame::OnToggleAllowDrag()
{
	mAllowDragging = !mAllowDragging;
}

void LiveOutlineFrame::OnToggleAutoExpand()
{
	mAutoExpand = !mAutoExpand;
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterAll()
{
	mFilterFlags.SetAll();
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterNone()
{
	mFilterFlags.ClearAll();
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleIncludes()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Includes);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleComments()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Comments);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleGlobals()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Globals);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleTags()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_TagsAndLabels);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleIfdefs()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Preprocessor);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleGroups()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_PseudoGroups);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleFunctions()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_MethodsAndFunctions);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleMacros()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Macros);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleVariables()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_MembersAndVariables);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleRegions()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Regions);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleEnums()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Enums);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleMsgMap()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_MessageMaps);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleTypes()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_TypesAndClasses);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnFilterToggleNamespaces()
{
	mFilterFlags.Flip(FileOutlineFlags::ff_Namespaces);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnSelectLayoutTree()
{
	if (!mLayoutAsList)
		return;

	mLayoutAsList = false;
	UpdateFilterSet();
}

void LiveOutlineFrame::OnSelectLayoutList()
{
	if (mLayoutAsList)
		return;

	mLayoutAsList = true;
	UpdateFilterSet();
}

void LiveOutlineFrame::UpdateFilterSet(bool retainVertScrollPos /*= true*/)
{
	mEdModCookie = 0;
	const int kPrevPos = mTree->GetScrollPos(SB_VERT);
	Refresh(WTString(), g_currentEdCnt);
	if (retainVertScrollPos)
		mTree->SetScrollPos(SB_VERT, kPrevPos);
}

// #define USE_FILTER_MENU

void LiveOutlineFrame::OnContextMenu(CWnd* /*pWnd*/, CPoint pos)
{
	mTree->PopTooltip();
	HTREEITEM selItem = mTree->GetSelectedItem();
	int selItemCnt = mTree->GetSelectedItemCount();

	// this assert will fire on rt-click after holding up or down until selection
	// moves beyond start or end of the tree.  selItem == non-null && selItemCnt == 0
	_ASSERTE((selItemCnt > 0 && selItem) || (!selItemCnt && !selItem));

	CRect rc;
	GetClientRect(&rc);
	ClientToScreen(&rc);
	if (!rc.PtInRect(pos) && selItem)
	{
		// place menu below selected item instead of at cursor when using
		// the context menu command
		if (mTree->GetItemRect(selItem, &rc, TRUE))
		{
			mTree->ClientToScreen(&rc);
			pos.x = rc.left + (rc.Width() / 2);
			pos.y = rc.bottom;
		}
	}

#ifdef USE_FILTER_MENU
	CMenu filterMenu;
	filterMenu.CreatePopupMenu();
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Includes) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_INCLUDES, "&Includes");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Comments) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_COMMENTS, "&Comments");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Globals) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_GLOBALS, "&File Scope Declarations");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_TagsAndLabels) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_TAGS, "&Labels and Tags");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Preprocessor) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_PREPROCESSOR, "Other &Preprocessor Directives");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_MethodsAndFunctions) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_METHODS, "&Methods and Functions");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_MembersAndVariables) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_MEMBERS, "Members and &Variables");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Macros) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_MACROS, "Macro&s");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Regions) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_REGIONS, "&Regions");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Enums) ? MF_CHECKED : 0u, ID_FILTER_TOGGLE_ENUMS,
	                      "&Enums");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_TypesAndClasses) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_TYPES, "&Types");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_MessageMaps) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_MSGMAP, "Messa&ge Maps");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Namespaces) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_NAMESPACES, "&Namespaces");
	filterMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_PseudoGroups) ? MF_CHECKED : 0u,
	                      ID_FILTER_TOGGLE_GROUPS, "&Group Methods, Includes and Macros");
	filterMenu.AppendMenu(MF_SEPARATOR);
	filterMenu.AppendMenu(0, ID_FILTER_ALL, "&All");
	filterMenu.AppendMenu(0, ID_FILTER_NONE, "C&lear All");
	filterMenu.AppendMenu(mFilterFlags == mFilterFlagsSaved1 ? MF_GRAYED | MF_DISABLED : 0u, ID_FILTER_LOAD1,
	                      "Load Display Set &1");
	filterMenu.AppendMenu(mFilterFlags == mFilterFlagsSaved2 ? MF_GRAYED | MF_DISABLED : 0u, ID_FILTER_LOAD2,
	                      "Load Display Set &2");
	filterMenu.AppendMenu(MF_SEPARATOR);
	filterMenu.AppendMenu(0, ID_FILTER_SAVEAS1, "Save As Display Set 1");
	filterMenu.AppendMenu(0, ID_FILTER_SAVEAS2, "Save As Display Set 2");
	CMenuXP::SetXPLookNFeel(this, filterMenu);
#endif // USE_FILTER_MENU

	const DWORD kSelectionDependentItemState = (selItem && mEdcnt) ? 0u : MF_GRAYED | MF_DISABLED;
	const DWORD kSingleSelDependentItemState = (selItemCnt == 1 && selItem && mEdcnt) ? 0u : MF_GRAYED | MF_DISABLED;

	CMenu contextMenu;
	contextMenu.CreatePopupMenu();
	contextMenu.AppendMenu(kSingleSelDependentItemState, MAKEWPARAM(ID_EDIT_GOTO, ICONIDX_REFERENCE_GOTO_DEF), "&Goto");
	contextMenu.AppendMenu(kSingleSelDependentItemState, ID_EDIT_SELECT, "&Select in Editor");

	// Refactor submenu
	CMenu refactorMenu;
	refactorMenu.CreatePopupMenu();
	if (selItemCnt == 1 && selItem)
	{
		const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
		if (mkr && !mkr->mClassData.IsEmpty())
		{
			DType symType = mkr->mClassData;
			DType contextType = GetContext(selItem);
			if (!PopulateRefactorMenu(refactorMenu, &symType, &contextType))
			{
				// caught an exception - queue a refresh
				if (g_ParserThread && g_currentEdCnt == mEdcnt)
				{
					g_ParserThread->QueueParseWorkItem(new RefreshFileOutline(mEdcnt));
				}
			}
		}
	}
	CMenuXP::SetXPLookNFeel(this, refactorMenu);

	ULONG refMenuAttr = MF_POPUP;
	if (!refactorMenu.GetMenuItemCount())
		refMenuAttr |= MF_GRAYED | MF_DISABLED;

	contextMenu.AppendMenu(refMenuAttr, (UINT_PTR)refactorMenu.m_hMenu, "Re&factor");

#ifndef RAD_STUDIO
	// #RAD_MissingFeature
	// Surround submenu
	contextMenu.AppendMenu(MF_SEPARATOR);
	CMenu surroundMenu;
	surroundMenu.CreatePopupMenu();
	if (selItemCnt > 0)
	{
		PopulateSurroundMenu(surroundMenu, (FileLineMarker*)mTree->GetItemData(selItem));
	}
	CMenuXP::SetXPLookNFeel(this, surroundMenu);

	ULONG surMenuAttr = MF_POPUP;
	if (!surroundMenu.GetMenuItemCount())
		surMenuAttr |= MF_GRAYED | MF_DISABLED;

	contextMenu.AppendMenu(surMenuAttr, (UINT_PTR)surroundMenu.m_hMenu, "Surround &With");
	if (gShellAttr->SupportsSelectionReformat())
		contextMenu.AppendMenu(kSelectionDependentItemState, ID_SURROUND_REFORMAT, "&Reformat");
#endif

	// Cut/Copy/Paste/Delete
	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_CUT, ICONIDX_CUT), "Cu&t");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_COPY, ICONIDX_COPY), "&Copy");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_PASTE, ICONIDX_PASTE), "&Paste");
	contextMenu.AppendMenu(kSelectionDependentItemState, MAKEWPARAM(ID_EDIT_CLEAR, ICONIDX_DELETE), "&Delete");

	// Expand/Collapse/Copy all
	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(0, ID_EXPAND_ALL, "&Expand All");
	contextMenu.AppendMenu(0, ID_COLLAPSE_ALL, "Co&llapse All");
	contextMenu.AppendMenu(0, ID_COPY_ALL, "Copy &Outline Content");

#ifdef _DEBUG
	contextMenu.AppendMenu(MF_SEPARATOR);
	contextMenu.AppendMenu(0, MAKEWPARAM(ID_REFRESH_NOW, ICONIDX_REPARSE), "Refres&h");
#endif // _DEBUG

	if (g_loggingEnabled && (0x8000 & GetKeyState(VK_SHIFT)))
	{
		contextMenu.AppendMenu(MF_SEPARATOR);
		contextMenu.AppendMenu(0, ID_LOG_OUTLINE, "&Log Contents");
	}

	contextMenu.AppendMenu(MF_SEPARATOR);

#ifdef USE_FILTER_MENU
	// filter submenu
	contextMenu.AppendMenu(MF_POPUP, (UINT_PTR)filterMenu.m_hMenu, "Displ&ay");
#else
	// display comment toggle
	contextMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Comments) ? MF_CHECKED : 0u,
	                       ID_FILTER_TOGGLE_COMMENTS, "Display Co&mments");
	contextMenu.AppendMenu(mFilterFlags.IsEnabled(FileOutlineFlags::ff_Regions) ? MF_CHECKED : 0u,
	                       ID_FILTER_TOGGLE_REGIONS, "Display Regio&ns");
	contextMenu.AppendMenu(mDisplayTooltips ? MF_CHECKED : 0u, ID_TOGGLE_DISPLAYTOOLTIPS, "Toolt&ips");

// 	contextMenu.AppendMenu(
// 		mFilterFlags.IsEnabled(FileOutlineFlags::ff_PseudoGroups) ? MF_CHECKED : 0u,
// 		ID_FILTER_TOGGLE_GROUPS,
// 		"Group Methods, &Includes and Macros");
#endif // !USE_FILTER_MENU

	contextMenu.AppendMenu(mAllowDragging ? MF_CHECKED : 0u, ID_TOGGLE_ALLOWDRAG, "&Allow Drag and Drop");
	contextMenu.AppendMenu(mAutoUpdate ? MF_CHECKED : 0u, ID_TOGGLE_AUTOUPDATE, "Auto &Update");
	contextMenu.AppendMenu(mAutoExpand ? MF_CHECKED : 0u, ID_TOGGLE_AUTOEXPAND, "Auto E&xpand Nodes");
	CMenuXP::SetXPLookNFeel(this, contextMenu);

	MenuXpHook hk(this);
	contextMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
}

HTREEITEM
LiveOutlineFrame::GetItemFromPos(POINT pos) const
{
	TVHITTESTINFO ht = {{0}};
	ht.pt = pos;
	::MapWindowPoints(HWND_DESKTOP, mTree->m_hWnd, &ht.pt, 1);

	if (mTree->HitTest(&ht) && (ht.flags & TVHT_ONITEM))
		return ht.hItem;

	return NULL;
}

CStringW LiveOutlineFrame::GetEditorSelection() const
{
	_ASSERTE(mEdcnt && mEdcnt == g_currentEdCnt);
	const CStringW curSelTxt = mEdcnt->GetSelStringW();
	return curSelTxt;
}

void LiveOutlineFrame::DeleteEditorSelection() const
{
	_ASSERTE(mEdcnt && mEdcnt == g_currentEdCnt);
	mEdcnt->Insert("");
}

bool LiveOutlineFrame::IsAllowedToStartDrag()
{
	if (mInTransitionPeriod || mUpdateIsPending || !mAllowDragging)
		return false;

	mEdModCookieAtDragStart = mEdModCookie;
	return true;
}

bool LiveOutlineFrame::IsAllowedToDrop(HTREEITEM target, bool afterTarget)
{
	if (!mEdcnt || mEdcnt != g_currentEdCnt || !mAllowDragging || mUpdateIsPending ||
	    mEdModCookie != mEdModCookieAtDragStart || mEdcnt->m_modCookie != mEdModCookie)
	{
		mTree->CancelDrag();
		return false;
	}

	const FileLineMarker* targetMkr = (FileLineMarker*)mTree->GetItemData(target);

	const TreeItemVec& sources = mTree->GetSelectedItems();
	const size_t numSources = sources.size();
	for (size_t i = 0; i < numSources; ++i)
	{
		HTREEITEM source = sources[i];
		if (source == target)
			return false;

		const FileLineMarker* sourceMkr = (FileLineMarker*)mTree->GetItemData(source);
		if (sourceMkr->mStartCp <= targetMkr->mStartCp && sourceMkr->mEndCp >= targetMkr->mEndCp)
		{
			return false;
		}

		if (!sourceMkr->mCanDrag || !targetMkr->mCanDrag)
			return false;
	}

	return true;
}

void LiveOutlineFrame::CopyDroppedItem(HTREEITEM target, bool afterTarget)
{
	_ASSERTE(mAllowDragging);
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !mTree->GetSelectedItemCount() || !target)
		return;

	UndoContext uc("Drag Copy");
	const FileLineMarker* targetMkr = (FileLineMarker*)mTree->GetItemData(target);
	int pasteCp = int(afterTarget ? targetMkr->mEndCp : targetMkr->mStartCp);
	int pasteLn = int(afterTarget ? targetMkr->mEndLine : targetMkr->mStartLine);

	InsertSelectedItemsText(pasteCp, pasteLn, false);
}

void LiveOutlineFrame::MoveDroppedItem(HTREEITEM target, bool afterTarget)
{
	_ASSERTE(mAllowDragging);
	const TreeItemVec& sources = mTree->GetSelectedItems();

	_ASSERTE(sources.size() && target);
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !sources.size() || !target)
		return;

	UndoContext uc("Drag Move");

	const FileLineMarker* targetMkr = (FileLineMarker*)mTree->GetItemData(target);
	int pasteCp = int(afterTarget ? targetMkr->mEndCp : targetMkr->mStartCp);
	int pasteLn = int(afterTarget ? targetMkr->mEndLine : targetMkr->mStartLine);

	size_t numSources = sources.size();
	for (size_t i = 0; i < numSources; ++i)
	{
		HTREEITEM source = sources[i];
		const FileLineMarker* sourceMkr = (FileLineMarker*)mTree->GetItemData(source);
		const int sourceCpCnt = int(sourceMkr->mEndCp - sourceMkr->mStartCp);
		const int sourceLnCnt = int(sourceMkr->mEndLine - sourceMkr->mStartLine);

		if (sourceMkr->mStartCp < targetMkr->mStartCp)
		{
			// source is before target - deleting source will invalidate insert position
			// so adjust based on size of source
			pasteCp -= sourceCpCnt;
			pasteLn -= sourceLnCnt;
		}
		else if (afterTarget && sourceMkr->mStartCp > targetMkr->mStartCp && sourceMkr->mStartCp < targetMkr->mEndCp)
		{
			// source is a child of target and being moved outside/after target
			// adjust insert position
			pasteCp -= sourceCpCnt;
			pasteLn -= sourceLnCnt;
		}
	}

	InsertSelectedItemsText(pasteCp, pasteLn, true);
}

void LiveOutlineFrame::OnSaveAsFilter1()
{
	mFilterFlagsSaved1 = mFilterFlags;
	::SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS_SAVED1, (DWORD)mFilterFlagsSaved1);
}

void LiveOutlineFrame::OnSaveAsFilter2()
{
	mFilterFlagsSaved2 = mFilterFlags;
	::SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS_SAVED2, (DWORD)mFilterFlagsSaved2);
}

void LiveOutlineFrame::OnLoadFilter1()
{
	mFilterFlags = mFilterFlagsSaved1 =
	    ::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS_SAVED1, kDefaultFlags);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnLoadFilter2()
{
	mFilterFlags = mFilterFlagsSaved2 =
	    ::GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, ID_RK_OUTLINE_FILTERFLAGS_SAVED2, kDefaultFlags);
	UpdateFilterSet();
}

void LiveOutlineFrame::OnRefreshNow()
{
	UpdateFilterSet(false);
}

void LiveOutlineFrame::DoRefactor(ULONG flag)
{
	RefactoringActive active;
	DB_READ_LOCK;

	HTREEITEM selItem = mTree->GetSelectedItem();
	const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
	DType* sym = const_cast<DType*>(&mkr->mClassData);
	if (sym)
	{
		DType context = GetContext(selItem);

		bool needsRefresh = true;
		try
		{
			switch (flag)
			{
			case VARef_FindUsage:
				if (gVaService)
					gVaService->FindReferences(FREF_Flg_Reference | FREF_Flg_Reference_Include_Comments |
					                               FREF_FLG_FindAutoVars,
					                           GetTypeImgIdx(sym->MaskedType(), sym->Attributes()), sym->SymScope());
				needsRefresh = false;
				break;

			case VARef_ChangeSignature:
				VARefactorCls::ChangeSignature(sym);
				break;

			case VARef_AddMember:
				VARefactorCls::AddMember(sym);
				break;

			case VARef_AddSimilarMember:
				VARefactorCls::AddMember(sym, sym);
				break;

			case VARef_Rename: {
				mEdcnt->SetFocusParentFrame(); // [case 1052] refresh problem after rename
				mEdcnt->vSetFocus();           // [case 7858] fix for reparse problems after rename

				RenameReferencesDlg ren(sym->SymScope());
				ren.DoModal();

				// rename happens on a thread - don't do an immediate refresh
				needsRefresh = false;
				mEdModCookie = 0;
			}
			break;

			case VARef_CreateMethodImpl: {
				VARefactorCls cls(sym->SymScope().c_str(), (int)mkr->mStartLine);
				cls.CreateImplementation(sym, context.SymScope());
			}
			break;

			case VARef_CreateMethodDecl: {
				VARefactorCls cls(sym->SymScope().c_str(), (int)mkr->mStartLine);
				cls.CreateDeclaration(sym, context.SymScope());
			}
			break;

			default:
				_ASSERTE(!"Not implemented yet");
				needsRefresh = false;
				break;
			}
		}
		catch (...)
		{
			VALOGEXCEPTION("LOF::DoRefactor:");
		}

		if (needsRefresh)
			OnModificationComplete();
	}
}

void LiveOutlineFrame::OnRefactorRename()
{
	DoRefactor(VARef_Rename);
}

void LiveOutlineFrame::OnRefactorFindRefs()
{
	DoRefactor(VARef_FindUsage);
}

void LiveOutlineFrame::OnRefactorAddMember()
{
	DoRefactor(VARef_AddMember);
}

void LiveOutlineFrame::OnRefactorAddSimilarMember()
{
	DoRefactor(VARef_AddSimilarMember);
}

void LiveOutlineFrame::OnRefactorChangeSignature()
{
	DoRefactor(VARef_ChangeSignature);
}

void LiveOutlineFrame::OnRefactorCreateImplementation()
{
	DoRefactor(VARef_CreateMethodImpl);
}

void LiveOutlineFrame::OnRefactorCreateDeclaration()
{
	DoRefactor(VARef_CreateMethodDecl);
}

static bool PopulateRefactorMenu(CMenu& menu, DType* sym, DType* context)
{
	if (sym)
	{
		try
		{
			DB_READ_LOCK;

			if (VARefactorCls::CanAddMember(sym))
				menu.AppendMenu(0, ID_REFACTOR_ADDMEMBER, "&Add member");
			if (VARefactorCls::CanAddSimilarMember(sym))
				menu.AppendMenu(0, ID_REFACTOR_ADDSIMILARMEMBER, "Add Similar Member");
			if (VARefactorCls::CanChangeSignature(sym))
				menu.AppendMenu(
				    0, MAKEWPARAM(ID_REFACTOR_CHANGESIG, ICONIDX_REFACTOR_CHANGE_SIGNATURE),
				    "Change S&ignature"); // use a different shortcut key than normal to avoid clash on &Goto
			if (VARefactorCls::CanRename(sym))
				menu.AppendMenu(0, MAKEWPARAM(ID_REFACTOR_RENAME, ICONIDX_REFACTOR_RENAME), "Re&name");
			WTString contextSymScope(context->SymScope());
			if (VARefactorCls::CanCreateDeclaration(sym, contextSymScope))
				menu.AppendMenu(0, ID_REFACTOR_CREATEDECLARATION, "&Create Declaration");
			if (VARefactorCls::CanCreateImplementation(sym, contextSymScope))
				menu.AppendMenu(0, ID_REFACTOR_CREATEIMPLEMENTATION, "Create I&mplementation");

			const BOOL kCanFindRefs = VARefactorCls::CanFindReferences(sym);
			if (menu.GetMenuItemCount() && kCanFindRefs)
				menu.AppendMenu(MF_SEPARATOR, 0, "");

			if (kCanFindRefs)
				menu.AppendMenu(0, MAKEWPARAM(ID_REFACTOR_FINDREFS, ICONIDX_REFERENCE_FIND_REF), "Find &References");
		}
		catch (...)
		{
			VALOGEXCEPTION("PRMenu:");
			return false;
		}
	}
	return true;
}

void LiveOutlineFrame::PopulateSurroundMenu(CMenu& menu, FileLineMarker* mkr)
{
	int commentIcon = ICONIDX_SNIPPET_COMMENTLINE;
	if (Is_Tag_Based(mEdcnt->m_ftype) || Is_VB_VBS_File(mEdcnt->m_ftype))
		commentIcon = 0;

	// [case: 22312] don't offer comments in html and asp because scope is
	// not accurate when running commands that change the selection from
	// the outline.  EdCnt uses scope to determine comment styles.  Due to
	// scope lag, js style comments are sometimes used in html, etc.
	if (mEdcnt->m_ftype != HTML && mEdcnt->m_ftype != ASP)
	{
		if (COMMENT == mkr->mType)
			menu.AppendMenu(0, MAKEWPARAM(ID_SURROUND_UNCOMMENT_CPP, commentIcon), "Un&comment");
		else
			menu.AppendMenu(0, MAKEWPARAM(ID_SURROUND_COMMENT_CPP, commentIcon), "&Comment");
	}

	if (CS == mEdcnt->m_ftype)
		menu.AppendMenu(0, ID_SURROUND_REGION, "#&region ... #endregion");
	if (Is_VB_VBS_File(mEdcnt->m_ftype))
		menu.AppendMenu(0, ID_SURROUND_REGION, "#&Region ... #End Region");
	if (IsCFile(mEdcnt->m_ftype) && gShellAttr->IsDevenv8OrHigher())
		menu.AppendMenu(0, ID_SURROUND_REGION, "#pragma &region ... #pragma endregion");
	if (IsCFile(mEdcnt->m_ftype))
		menu.AppendMenu(0, MAKEWPARAM(ID_SURROUND_IFDEF, ICONIDX_SNIPPET_IFDEF), "&#ifdef ... #endif");
	if (IsCFile(mEdcnt->m_ftype) || mEdcnt->m_ftype == CS)
		menu.AppendMenu(0, ID_SURROUND_NAMESPACE, "&Namespace");
}

void LiveOutlineFrame::OnTripleClick()
{
	OnSelectItemInEditor();
}

static DragDropTreeCtrl* gTree = NULL;

static bool PV_SortByCp(const HTREEITEM& lhs, const HTREEITEM& rhs)
{
	_ASSERTE(gTree);
	const FileLineMarker* mkr1 = (FileLineMarker*)gTree->GetItemData(lhs);
	const FileLineMarker* mkr2 = (FileLineMarker*)gTree->GetItemData(rhs);
	if (mkr1->mStartCp == mkr2->mStartCp)
		return mkr1->mEndCp > mkr2->mEndCp;
	else
		return mkr1->mStartCp > mkr2->mStartCp;
}

static void PV_SortSelectedItemsVec(TreeItemVec& selItems, DragDropTreeCtrl* tree)
{
	// sort high-to-low.  I don't like the static tree ptr, but don't
	// want to write my own sort.
	gTree = tree;
	std::sort(selItems.begin(), selItems.end(), PV_SortByCp);
	gTree = NULL;
}

CStringW LiveOutlineFrame::GetSelectedItemsText(bool deleteFromEditor)
{
	CStringW txt;

	TreeItemVec selItems = mTree->GetSelectedItems(); // copy
	PV_SortSelectedItemsVec(selItems, mTree);

	const size_t numSelected = selItems.size();
	const CStringW eol(mEdcnt->GetLineBreakString().Wide());

	for (size_t i = 0; i < numSelected; ++i)
	{
		HTREEITEM selItem = selItems[i];
		const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);

		SelectLinesInEditor((int)mkr->mEndLine - 1, (int)mkr->mEndLine);
		CStringW tmpTxt = GetEditorSelection();
		bool atEof = tmpTxt.Right(eol.GetLength()) != eol;

		SelectCharsInEditor((int)mkr->mStartCp, (int)mkr->mEndCp);
		tmpTxt = GetEditorSelection();

		if (atEof)
		{
			tmpTxt += eol;
			if (deleteFromEditor)
			{
				// Eat EOL if precedes txt.
				long testStartCp = (long)mkr->mStartCp;
				if (testStartCp >= eol.GetLength())
					testStartCp -= eol.GetLength(); // move start pos back to eat linebreak
				SelectCharsInEditor(testStartCp, (int)mkr->mEndCp);
				if (GetEditorSelection().Left(eol.GetLength()) == eol)
				{
					// leave selection, leading EOL gets eated.
				}
				else
				{
					// restore orig selection
					SelectCharsInEditor((int)mkr->mStartCp, (int)mkr->mEndCp);
				}
			}
		}

		txt = tmpTxt + txt;
		if (deleteFromEditor)
			DeleteEditorSelection();
	}

	return txt;
}

DWORD
LiveOutlineFrame::QueryStatus(DWORD cmdId) const
{
	switch (cmdId)
	{
	case icmdVaCmd_OutlineFind:
	case icmdVaCmd_OutlineFindNext:
	case icmdVaCmd_OutlineFindPrev:
		return 0u;
	case icmdVaCmd_OutlineCopy:
	case icmdVaCmd_OutlineCut:
	case icmdVaCmd_OutlineDelete:
	case icmdVaCmd_OutlinePaste:
		return 1u;
	case icmdVaCmd_OutlineSelect:
	case icmdVaCmd_OutlineGoto:
	case icmdVaCmd_SurroundWithPreprocDirective:
		return mTree->GetSelectedItem() ? 1u : 0u;
	case icmdVaCmd_OutlineFilterSelectAll:
	case icmdVaCmd_OutlineFilterClearAll:
	case icmdVaCmd_OutlineLoadFilterSet1:
	case icmdVaCmd_OutlineLoadFilterSet2:
	case icmdVaCmd_OutlineSaveAsFilterSet1:
	case icmdVaCmd_OutlineSaveAsFilterSet2:
	case icmdVaCmd_OutlineRefresh:
	case icmdVaCmd_OutlineRedo:
	case icmdVaCmd_OutlineUndo:
	case icmdVaCmd_OutlineContextMenu:
		return 1;
	case icmdVaCmd_OutlineToggleCommentDisplay:
		return 1 | (mFilterFlags.IsEnabled(FileOutlineFlags::ff_Comments) ? 0x80000000 : 0u); // latching command
	case icmdVaCmd_OutlineToggleAutoupdate:
		return 1 | (mAutoUpdate ? 0x80000000 : 0u); // latching command
	case icmdVaCmd_RefactorExtractMethod:
	case icmdVaCmd_RefactorPromoteLambda:
	case icmdVaCmd_RefactorEncapsulateField:
	case icmdVaCmd_RefactorMoveImplementation:
	case icmdVaCmd_RefactorDocumentMethod:
	case icmdVaCmd_RefactorChangeVisibility:
	case icmdVaCmd_RefactorPopupMenu:
	case icmdVaCmd_RefactorExpandMacro:
	case icmdVaCmd_RefactorRenameFiles:
	case icmdVaCmd_RefactorCreateFile:
	case icmdVaCmd_RefactorMoveSelToNewFile:
	case icmdVaCmd_RefactorMoveImplementationToHdr:
	case icmdVaCmd_RefactorConvertBetweenPointerAndInstance:
	case icmdVaCmd_RefactorSimplifyInstanceDeclaration:
	case icmdVaCmd_RefactorAddForwardDeclaration:
	case icmdVaCmd_RefactorConvertEnum:
	case icmdVaCmd_RefactorMoveClassToNewFile:
	case icmdVaCmd_RefactorSortClassMethods:
		// not supported by DoRefactor (=not available in VA Outline)
		return 0u;
	case icmdVaCmd_RefactorCreateDeclaration:
	case icmdVaCmd_RefactorCreateImplementation:
	case icmdVaCmd_RefactorAddMember:
	case icmdVaCmd_RefactorAddSimilarMember:
	case icmdVaCmd_RefactorChangeSignature:
	case icmdVaCmd_RefactorRename:
	case icmdVaCmd_FindReferences:
		if (mTree->GetSelectedItemCount() == 1)
		{
			HTREEITEM selItem = mTree->GetSelectedItem();
			if (selItem)
			{
				const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
				if (mkr && !mkr->mClassData.IsEmpty())
				{
					try
					{
						DB_READ_LOCK;
						if (icmdVaCmd_RefactorAddMember == cmdId)
							return VARefactorCls::CanAddMember(const_cast<DType*>(&mkr->mClassData)) ? 1u : 0u;
						if (icmdVaCmd_RefactorAddSimilarMember == cmdId)
							return VARefactorCls::CanAddSimilarMember(const_cast<DType*>(&mkr->mClassData)) ? 1u : 0u;
						if (icmdVaCmd_RefactorChangeSignature == cmdId)
							return VARefactorCls::CanChangeSignature(const_cast<DType*>(&mkr->mClassData)) ? 1u : 0u;
						if (icmdVaCmd_RefactorRename == cmdId)
							return VARefactorCls::CanRename(const_cast<DType*>(&mkr->mClassData)) ? 1u : 0u;
						if (icmdVaCmd_FindReferences == cmdId)
							return VARefactorCls::CanFindReferences(const_cast<DType*>(&mkr->mClassData)) ? 1u : 0u;
						if (icmdVaCmd_RefactorCreateDeclaration == cmdId)
							return VARefactorCls::CanCreateDeclaration(const_cast<DType*>(&mkr->mClassData),
							                                           const_cast<DType*>(&mkr->mClassData)->Scope())
							           ? 1u
							           : 0u;
						if (icmdVaCmd_RefactorCreateImplementation == cmdId)
							return VARefactorCls::CanCreateImplementation(const_cast<DType*>(&mkr->mClassData),
							                                              const_cast<DType*>(&mkr->mClassData)->Scope())
							           ? 1u
							           : 0u;
					}
					catch (...)
					{
						VALOGEXCEPTION("PRMenu:");
					}
				}
			}
		}
		return 0u;
	}

	return (DWORD)-2;
}

HRESULT
LiveOutlineFrame::Exec(DWORD cmdId)
{
	if (!g_currentEdCnt)
	{
		mEdcnt = NULL;
		Clear();
	}

	switch (cmdId)
	{
	case icmdVaCmd_OutlineFind:
	case icmdVaCmd_OutlineFindNext:
	case icmdVaCmd_OutlineFindPrev:
		return E_UNEXPECTED;
	case icmdVaCmd_OutlineSelect:
		OnSelectItemInEditor();
		break;
	case icmdVaCmd_OutlineToggleAutoupdate:
		OnToggleAutoupdate();
		break;
	case icmdVaCmd_OutlineFilterSelectAll:
		OnFilterAll();
		break;
	case icmdVaCmd_OutlineFilterClearAll:
		OnFilterNone();
		break;
	case icmdVaCmd_OutlineLoadFilterSet1:
		OnLoadFilter1();
		break;
	case icmdVaCmd_OutlineLoadFilterSet2:
		OnLoadFilter2();
		break;
	case icmdVaCmd_OutlineSaveAsFilterSet1:
		OnSaveAsFilter1();
		break;
	case icmdVaCmd_OutlineSaveAsFilterSet2:
		OnSaveAsFilter2();
		break;
	case icmdVaCmd_OutlineCopy:
		OnCopy();
		break;
	case icmdVaCmd_OutlineCut:
		OnCut();
		break;
	case icmdVaCmd_OutlineDelete:
		OnDelete();
		break;
	case icmdVaCmd_OutlinePaste:
		OnPaste();
		break;
	case icmdVaCmd_OutlineRefresh:
		OnRefreshNow();
		break;
	case icmdVaCmd_OutlineGoto:
		OnGoto();
		break;
	case icmdVaCmd_OutlineContextMenu: {
		CPoint pos;
		::GetCaretPos(&pos);
		OnContextMenu(NULL, pos);
	}
	break;
	case icmdVaCmd_OutlineRedo:
		if (mEdcnt && g_currentEdCnt == mEdcnt)
		{
			static const char* const kActivateWindow = "Window.ActivateDocumentWindow";
			SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM)kActivateWindow, 0);
			static const char* const kRedo = "Edit.Redo";
			::PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)kRedo, 0);
			RequestRefresh();
		}
		break;
	case icmdVaCmd_OutlineUndo:
		if (mEdcnt && g_currentEdCnt == mEdcnt)
		{
			static const char* const kActivateWindow = "Window.ActivateDocumentWindow";
			SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM)kActivateWindow, 0);
			static const char* const kUndo = "Edit.Undo";
			::PostMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)kUndo, 0);
			RequestRefresh();
		}
		break;
	case icmdVaCmd_OutlineToggleCommentDisplay:
		OnFilterToggleComments();
		break;
	case icmdVaCmd_SurroundWithPreprocDirective:
		OnSurroundIfdef();
		break;
	case icmdVaCmd_RefactorAddMember:
		OnRefactorAddMember();
		break;
	case icmdVaCmd_RefactorAddSimilarMember:
		OnRefactorAddSimilarMember();
		break;
	case icmdVaCmd_RefactorChangeSignature:
		OnRefactorChangeSignature();
		break;
	case icmdVaCmd_RefactorCreateDeclaration:
		OnRefactorCreateDeclaration();
		break;
	case icmdVaCmd_RefactorCreateImplementation:
		OnRefactorCreateImplementation();
		break;
	case icmdVaCmd_FindReferences:
		OnRefactorFindRefs();
		break;
	case icmdVaCmd_RefactorRename:
		OnRefactorRename();
		break;
	default:
		return OLECMDERR_E_NOTSUPPORTED;
	}

	return S_OK;
}

void LiveOutlineFrame::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kRefreshTimerId)
	{
		mRefreshTimerId = 0;
		KillTimer(nIDEvent);
		OnRefreshNow();
		return;
	}

	__super::OnTimer(nIDEvent);
}

void LiveOutlineFrame::InsertSelectedItemsText(int pasteCp, int pasteLn, bool deleteFromEditor)
{
	CStringW selTxt = GetSelectedItemsText(deleteFromEditor);

	const CStringW eol(mEdcnt->GetLineBreakString().Wide());
	// check if we're adding at EOF without final EOL
	SelectLinesInEditor(pasteLn - 1, pasteLn);
	CStringW tmpTxt = GetEditorSelection();

	if (tmpTxt.GetLength() > 0)
	{
		if (tmpTxt.Right(eol.GetLength()) != eol)
		{
			if (selTxt.Right(eol.GetLength()) == eol)
			{
				// move EOL from end to beginning
				selTxt = eol + selTxt.Left(selTxt.GetLength() - eol.GetLength());
			}
			else
			{
				// prepend EOL - this causes file growth
				selTxt = eol + selTxt;
			}
		}
	}
	else
	{
		// there was no editor selection because pasteLine is beyond EOF

		// add implied EOL to previous node
		selTxt = eol + selTxt;

		// strip actual EOL at end of from this node (becomes implied),
		// but leave at least one actual EOL.
		if (selTxt.Right(eol.GetLength() * 2) == (eol + eol))
		{
			selTxt = selTxt.Left(selTxt.GetLength() - eol.GetLength());
		}
	}

	MoveToEditorCp(pasteCp);
	// [case: 53417] don't treat drag and drop as paste - no formatAfterPaste
	mEdcnt->InsertW(selTxt, true, noFormat, false);
	MoveToEditorCp(pasteCp);
	OnModificationComplete();
}

void LiveOutlineFrame::DoSurround(UINT msg, UINT cmd)
{
	const size_t kNumSelItems = (size_t)mTree->GetSelectedItemCount();
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !kNumSelItems)
		return;

	// sort so that last in file is first in vec
	TreeItemVec selVec = mTree->GetSelectedItems(); // copy
	PV_SortSelectedItemsVec(selVec, mTree);

	UndoContext uc("Surround");

	int startLine = 0;
	int endLine = 0;
	int startCp = 0;
	int endCp = 0;
	bool firstTime = true;

	for (size_t i = 0; i < kNumSelItems; ++i)
	{
		HTREEITEM selItem = selVec[i];
		const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selItem);
		if (!mkr)
		{
			// build 1859 wer event id -1565502841
			_ASSERTE(mkr);
			vLog("ERROR: missing mkr during LOF::DoSurround");
			continue;
		}

		if (firstTime)
		{
			startLine = (int)mkr->mStartLine;
			endLine = (int)mkr->mEndLine;
			startCp = (int)mkr->mStartCp;
			endCp = (int)mkr->mEndCp;
			firstTime = false;
		}

		if (i + 1 < kNumSelItems)
		{
			HTREEITEM nextSelItem = selVec[i + 1];
			const FileLineMarker* nextMkr = (FileLineMarker*)mTree->GetItemData(nextSelItem);

			if (nextMkr->mEndCp == mkr->mStartCp)
			{
				// contiguous nodes
				startLine = (int)nextMkr->mStartLine;
				startCp = (int)nextMkr->mStartCp;
				continue;
			}

			// 			if (nextMkr->mEndLine == mkr->mStartLine)
			// 			{
			// 				// contiguous nodes
			// 				startLine = nextMkr->mStartLine;
			// 				startCp = nextMkr->mStartCp;
			// 				continue;
			// 			}
		}

		DelayFileOpen(mEdcnt->FileName(), startLine);
		_ASSERTE(endLine >= startLine);
		_ASSERTE(endCp > startCp);
		//		SelectLinesInEditor(endLine, startLine);
		SelectCharsInEditor(endCp, startCp);
		mEdcnt->SendMessage(msg, cmd, 0);
		firstTime = true;
	}

	OnModificationComplete();

	// [case: 81739]
	DelayFileOpen(mEdcnt->FileName());
}

void LiveOutlineFrame::OnSurroundCommentC()
{
}

void LiveOutlineFrame::OnSurroundUncommentC()
{
}

void LiveOutlineFrame::OnSurroundCommentCPP()
{
	if (mFilterFlags.IsEnabled(FileOutlineFlags::ff_Comments))
	{
		// [case: 22312 / case: 41193] better comment handling if comments are displayed
		DoSurround(WM_VA_SMARTCOMMENT, 0);
	}
	else
	{
		// [case: 41193] old comment handling if comments are not displayed
		DoSurround(WM_COMMAND, VAM_COMMENTBLOCK2);
		// [TODO case 41198] need to determine whether VAM_COMMENTBLOCK or VAM_COMMENTBLOCK2
		// is appropriate; use VAM_COMMENTBLOCK if sub-line node, else use VAM_COMMENTBLOCK2
	}
}

void LiveOutlineFrame::OnSurroundUncommentCPP()
{
	DoSurround(WM_VA_SMARTCOMMENT, 0);
}

void LiveOutlineFrame::OnSurroundIfdef()
{
	DoSurround(WM_COMMAND, VAM_IFDEFBLOCK);
}

void LiveOutlineFrame::OnSurroundRegion()
{
	DoSurround(WM_COMMAND, VAM_REGIONBLOCK);
}

void LiveOutlineFrame::OnSurroundNamespace()
{
	DoSurround(WM_COMMAND, VAM_NAMESPACEBLOCK);
}

void LiveOutlineFrame::OnSurroundReformat()
{
	const size_t kNumSelItems = (size_t)mTree->GetSelectedItemCount();
	if (!g_currentEdCnt || mEdcnt != g_currentEdCnt || !kNumSelItems)
		return;

	const TreeItemVec& selVec = mTree->GetSelectedItems();

	UndoContext uc("Surround Reformat");

	for (size_t i = 0; i < kNumSelItems; ++i)
	{
		const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(selVec[i]);
		DelayFileOpen(mEdcnt->FileName(), (int)mkr->mStartLine);
		_ASSERTE(mkr->mEndLine > mkr->mStartLine);
		SelectLinesInEditor((int)mkr->mEndLine, (int)mkr->mStartLine);
		gShellSvc->FormatSelection();
	}

	const FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(mTree->GetSelectedItem());
	MoveToEditorLine((int)mkr->mStartLine);
	OnModificationComplete();
}

void LiveOutlineFrame::OnLogOutline()
{
	if (!mEdcnt)
		return;

	WTString fileText = mEdcnt->GetBuf(TRUE);

	// what is this stuff?
	const WTString padding = "\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n";
	if (fileText.EndsWith(padding))
		fileText = fileText.Mid(0, fileText.GetLength() - padding.GetLength());

	LogFileOutline(fileText, mEdcnt->GetParseDb(), VaDirs::GetDbDir() + L"VAOutline.log");
}

CStringW LiveOutlineFrame::GetOutlineState() const
{
	CStringW sum, tmp;
	EdCntPtr ed(mEdcnt);
	if (!ed)
	{
		sum = L"ERROR: no editor for outline";
		return sum;
	}

	CString__FormatW(sum, L"VA Outline: %s 0x%lx\r\n", (LPCWSTR)::Basename(ed->FileName()), (DWORD)mFilterFlags);

	HTREEITEM hti = mTree->GetRootItem();

	// walk tree
	while (hti)
	{
		if (TVIS_EXPANDED & mTree->GetItemState(hti, TVIS_EXPANDED))
			sum += L"+ ";
		else if (mTree->ItemHasChildren(hti))
			sum += L"- ";
		else
			sum += L"  ";

		FileLineMarker* mkr = (FileLineMarker*)mTree->GetItemData(hti);
		if (mkr)
			CString__FormatW(tmp, L"%s 0x%lx 0x%lx %ld %ld %ld 0x%lx 0x%lx\r\n", (LPCWSTR)mkr->mText, mkr->mType,
			                 mkr->mAttrs, mkr->mGotoLine, mkr->mStartLine, mkr->mEndLine, mkr->mStartCp, mkr->mEndCp);
		else
			tmp = L"ERROR: no mkr for node\r\n";
		sum += tmp;

		hti = GetNextTreeItem(hti);
	}

	// finishing

	return sum;
}

static int PV_GetItemAtLine(DragDropTreeCtrl* tree, HTREEITEM hItem, int line, HTREEITEM* outRsltItem,
                            bool visibleOnly = true)
{
	while (hItem)
	{
		FileLineMarker* mkr = (FileLineMarker*)tree->GetItemData(hItem);
		if (mkr)
		{
			*outRsltItem = hItem;
			if (line < (int)mkr->mStartLine)
			{
				return -1;
			}
			else if (line < (int)mkr->mEndLine)
			{
				if (tree->ItemHasChildren(hItem) &&
				    (!visibleOnly || (TVIS_EXPANDED & tree->GetItemState(hItem, TVIS_EXPANDED))))
				{
					HTREEITEM childItem = tree->GetChildItem(hItem);
					return PV_GetItemAtLine(tree, childItem, line, outRsltItem, visibleOnly);
				}
				else
				{
					return 0;
				}
			}
			else
			{
				hItem = tree->GetNextSiblingItem(hItem);
			}
		}
	}
	return 1;
}

void LiveOutlineFrame::HighlightItemAtLine(int line)
{
	if (g_currentEdCnt != mEdcnt || mInTransitionPeriod || mUpdateIsPending)
		return;

	HTREEITEM rsltItem = NULL;
	HTREEITEM hItem = mTree->GetRootItem();

	int rslt = PV_GetItemAtLine(mTree, hItem, line, &rsltItem, !mAutoExpand);
	if (rslt != 0)
	{
		// use parent if we're either before first sibling or after last.
		HTREEITEM tmp = mTree->GetParentItem(rsltItem);
		if (!tmp)
		{
			// we're at root level, so use returned item
			tmp = rsltItem;
		}
		rsltItem = tmp;
	}

	if (rsltItem && rsltItem != mLastBolded)
	{
		UpdateWindow();
		mTree->SetNoopErase(false);
		HTREEITEM prevBoldItem = mLastBolded;
		HTREEITEM prevFirstVisible = mTree->GetFirstVisibleItem();
		mLastBolded = rsltItem;
		mTree->SetRedraw(FALSE);

#pragma warning(push)
#pragma warning(disable : 4127)
		if (0 && mAutoExpand)
		{
			HTREEITEM tmp = prevBoldItem;
			while (tmp)
			{
				mTree->Expand(tmp, TVE_COLLAPSE);
				tmp = mTree->GetParentItem(tmp);
			}
		}
#pragma warning(pop)

		const int kOldHscrollPos = mTree->GetScrollPos(SB_HORZ);
		mTree->EnsureVisible(mAutoExpand ? rsltItem : GetLastUnexpandedParent(*mTree, rsltItem));
		mTree->SendMessage(WM_HSCROLL, SB_LEFT, 0);
		const int kNewHscrollPos = mTree->GetScrollPos(SB_HORZ);
		HTREEITEM newFirstVisible = mTree->GetFirstVisibleItem();
		// [case: 76916]
		// the SB_LEFT call causes the new bold item to flash when this SetItemState call is
		// before the SB_LEFT call
		mTree->SetItemState(rsltItem, TVIS_BOLD, TVIS_BOLD);

		if (newFirstVisible == prevFirstVisible && kNewHscrollPos == kOldHscrollPos)
		{
			if (mAutoExpand)
			{
				mTree->SetItemState(prevBoldItem, 0, TVIS_BOLD);
				mTree->SetRedraw(TRUE);
				mTree->RedrawWindow();
			}
			else
			{
				CRect rc1, rc2;
				mTree->GetItemRect(mLastBolded, &rc2, FALSE);
				if (prevBoldItem)
					mTree->GetItemRect(prevBoldItem, &rc1, FALSE);

				mTree->SetRedraw(TRUE);
				// validate entire window
				mTree->RedrawWindow(NULL, NULL, RDW_VALIDATE | RDW_NOERASE | RDW_NOINTERNALPAINT);

				// invalidate only prevBoldItem and mLastBolded
				if (prevBoldItem)
				{
					mTree->SetItemState(prevBoldItem, 0, TVIS_BOLD);
					mTree->RedrawWindow(&rc1, NULL, RDW_INVALIDATE | RDW_ERASE); // erase since old bold was longer
				}
				mTree->RedrawWindow(&rc2, NULL,
				                    RDW_UPDATENOW | RDW_INVALIDATE | RDW_NOERASE); // no erase, bold text is longer
			}
		}
		else if (newFirstVisible == mLastBolded && kNewHscrollPos == kOldHscrollPos)
		{
			mTree->SetItemState(prevBoldItem, 0, TVIS_BOLD);
			for (int idx = 0; idx < 5; ++idx)
				mTree->SendMessage(WM_VSCROLL, SB_LINEUP, 0);
			mTree->SetRedraw(TRUE);
			mTree->RedrawWindow();
		}
		else
		{
			mTree->SetItemState(prevBoldItem, 0, TVIS_BOLD);
			for (int idx = 0; idx < 5; ++idx)
				mTree->SendMessage(WM_VSCROLL, SB_LINEDOWN, 0);
			mTree->SetRedraw(TRUE);
			mTree->RedrawWindow();
		}

		mTree->SetNoopErase(true);
	}
}

void LiveOutlineFrame::RequestRefresh(UINT delay /*= 50u*/)
{
	if (mRefreshTimerId)
		KillTimer(mRefreshTimerId);
	mRefreshTimerId = SetTimer(kRefreshTimerId, delay, NULL);
}

void LiveOutlineFrame::OnModificationComplete()
{
	mSelectItemDuringRefresh = true;
	if (gShellAttr->IsMsdev())
	{
		// [case 8029] getbuf fails if outline has focus
		mEdcnt->SetFocusParentFrame();
		UpdateFilterSet();
	}
	else
	{
		// it would logically make sense to move this UpdateFilterSet call to
		// the end of the method so that there was just a single call in this
		// method - but the vc6 is an aberration - it used to be as follows.
		// Don't want to change the order in VS - this order is known to work.
		// Could try changing it right after 1604...
		UpdateFilterSet();
		gShellSvc->GotoVaOutline();
	}
	mSelectItemDuringRefresh = false;
}

bool LiveOutlineFrame::IsWindowFocused() const
{
	if (m_hWnd && ::IsWindow(m_hWnd) && IsWindowVisible())
	{
		CWnd* foc = GetFocus();
		if (foc)
		{
			if (foc == this || foc == mTree)
			{
				return true;
			}
		}
	}
	return false;
}

void LiveOutlineFrame::OnEdCntClosing(const EdCntPtr& edcnt)
{
	// just to be safe
	if (mEdcnt == edcnt)
	{
		mEdcnt = NULL;
		Clear();
	}
	if (mEdcnt_last_refresh == edcnt)
		mEdcnt_last_refresh = NULL;
}

bool LiveOutlineFrame::IsMsgTarget(HWND hWnd) const
{
	if (hWnd == m_hWnd || hWnd == mTree->GetSafeHwnd())
		return true;
	return false;
}

DType LiveOutlineFrame::GetContext(HTREEITEM item)
{
	for (;;)
	{
		HTREEITEM parentItem = mTree->GetParentItem(item);
		if (parentItem)
		{
			FileLineMarker* parentMkr = (FileLineMarker*)mTree->GetItemData(parentItem);
			if (parentMkr && !parentMkr->mClassData.IsEmpty())
			{
				return parentMkr->mClassData;
			}

			item = parentItem;
		}
		else
		{
			break;
		}
	}
	return DType();
}

BOOL LiveOutlineFrame::OnEraseBkgnd(CDC* dc)
{
	if (CVS2010Colours::IsVS2010VAOutlineColouringActive())
		return 1;
	else
		return __super::OnEraseBkgnd(dc);
}

void LiveOutlineFrame::ThemeUpdated()
{
	gImgListMgr->SetImgListForDPI(*mTree, ImageListManager::bgTree, TVSIL_NORMAL);
	RedrawWindow(NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

HTREEITEM LiveOutlineFrame::GetNextTreeItem(HTREEITEM hti) const
{
	// has this item got any children
	if (mTree->ItemHasChildren(hti))
	{
		return mTree->GetNextItem(hti, TVGN_CHILD);
	}
	else if (mTree->GetNextItem(hti, TVGN_NEXT) != NULL)
	{
		// the next item at this level
		return mTree->GetNextItem(hti, TVGN_NEXT);
	}
	else
	{
		// return the next item after our parent
		hti = mTree->GetParentItem(hti);
		if (hti == NULL)
		{
			// no parent
			return NULL;
		}
		while (mTree->GetNextItem(hti, TVGN_NEXT) == NULL)
		{
			hti = mTree->GetParentItem(hti);
			if (hti == NULL)
				return NULL;
		}
		// next item that follows our parent
		return mTree->GetNextItem(hti, TVGN_NEXT);
	}
}

void LiveOutlineFrame::ExpandOrCollapseAll(UINT expandFlag)
{
	// preparation
	CWaitCursor curs;
	SetRedraw(FALSE);
	HTREEITEM hti_selected = mTree->GetSelectedItem();
	HTREEITEM hti = mTree->GetRootItem();

	// expand all
	while (hti)
	{
		CString itemText = mTree->GetItemText(hti);
		if (expandFlag == TVE_EXPAND || (itemText != "public:" && itemText != "private:" && itemText != "protected:" &&
		                                 itemText != "__published:" && itemText != "published:"))
		{
			if (mTree->ItemHasChildren(hti))
			{
				mTree->Expand(hti, expandFlag);
			}
		}

		hti = GetNextTreeItem(hti);
	}

	// finishing
	mTree->Select(hti_selected, TVGN_CARET); // restore user selection
	SetRedraw(TRUE);
	RedrawWindow();
}

void LiveOutlineFrame::OnExpandAll()
{
	ExpandOrCollapseAll(TVE_EXPAND);
}

void LiveOutlineFrame::OnCollapseAll()
{
	ExpandOrCollapseAll(TVE_COLLAPSE);
}

CStringW LiveOutlineFrame::CopyHierarchy(HTREEITEM item, CStringW prefix)
{
	CStringW txt(prefix + CStringW(mTree->GetItemText(item)) + L"\r\n");
	if (mTree->GetItemState(item, TVIS_EXPANDED) & TVIS_EXPANDED)
	{
		item = mTree->GetNextItem(item, TVGN_CHILD);
		while (item)
		{
			txt += CopyHierarchy(item, prefix + CStringW(L"\t"));
			item = mTree->GetNextSiblingItem(item);
		}
	}

	return txt;
}

void LiveOutlineFrame::CopyAll()
{
	CWaitCursor curs;

	CStringW txt;
	HTREEITEM hti = mTree->GetRootItem();
	txt += CopyHierarchy(hti, CStringW());
	while ((hti = mTree->GetNextItem(hti, TVGN_NEXT)) != NULL)
		txt += CopyHierarchy(hti, CStringW());

	::SaveToClipboard(m_hWnd, txt);

	if (gTestLogger)
	{
		gTestLogger->LogStr("Outline Copy All start:");
		gTestLogger->LogStr(WTString(txt));
		gTestLogger->LogStr("Outline Copy All end.");
	}
}

void LiveOutlineFrame::OnCopyAll()
{
	CopyAll();
}

LineMarkersPtr LiveOutlineFrame::GetMarkers() const
{
	AutoLockCs l(mMarkersLock);
	return mMarkers;
}

// RefreshFileOutline
// ----------------------------------------------------------------------------
//
RefreshFileOutline::RefreshFileOutline(EdCntPtr ed) : ParseWorkItem("RefreshFileOutline"), mEdModCookie(0)
{
	try
	{
		mEdBuf = ed->GetBuf(TRUE);
		mEdModCookie = ed->m_modCookie;
		mEd = ed;
	}
	catch (...)
	{
	}
}

void RefreshFileOutline::DoParseWork()
{
	if (!gVaService || !gVaService->GetOutlineFrame() || gShellIsUnloading)
		return;

	try
	{
		gVaService->GetOutlineFrame()->Refresh(mEdBuf, mEd, mEdModCookie);
	}
	catch (...)
	{
		VALOGEXCEPTION("RefreshOutline:");
		_ASSERTE(!"RefreshOutline exception caught - let sean know if this happens");
	}
}
