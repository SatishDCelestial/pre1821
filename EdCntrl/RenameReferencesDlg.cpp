#include "StdAfxEd.h"
#include "RenameReferencesDlg.h"
#include "VACompletionBox.h"
#include "EdCnt.h"
#include "UndoContext.h"
#include "VaMessages.h"
#include "FreezeDisplay.h"
#include "StatusBarAnimator.h"
#include "DevShellAttributes.h"
#include "FindReferencesThread.h"
#include "VARefactor.h"
#include "Settings.h"
#include "VAAutomation.h"
#include "WindowUtils.h"
#include "MenuXP\MenuXP.h"
#include "VaService.h"
#include "Colourizer.h"
#include "FILE.H"
#include "ProjectInfo.h"
#include "FileId.h"
#include "SymbolPositions.h"
#include "fdictionary.h"
#include <filesystem>
#include "VAWatermarks.h"
#include "Expansion.h"

#ifdef RAD_STUDIO
#include "CppBuilder.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define ID_CMD_CHECK_ALL 0x304
#define ID_CMD_UNCHECK_ALL 0x305
#define ID_CMD_CHECK_ALL_COMMENTS 0x306
#define ID_CMD_UNCHECK_ALL_COMMENTS 0x307
#define ID_CMD_CHECK_ALL_INCLUDES 0x308
#define ID_CMD_UNCHECK_ALL_INCLUDES 0x309
#define ID_CMD_CHECK_ALL_OVERRIDES 0x30a
#define ID_CMD_UNCHECK_ALL_OVERRIDES 0x30b
#define ID_CMD_CHECK_ALL_UNKNOWN 0x30c
#define ID_CMD_UNCHECK_ALL_UNKNOWN 0x30d

extern BOOL gIgnoreRestrictedFileType;  // [case: 147774] defined in file.cpp

CStringW GetProjectName(CStringW fileName, bool noPath = true);

CStringW GetFilePathByExtension(CStringW pathToSearch, CStringW extension)
{
	CStringW retVal;

	for (const auto& entry : std::filesystem::directory_iterator(pathToSearch.GetString()))
	{
		if (std::filesystem::is_regular_file(entry))
		{
			if (entry.path().extension() == extension.GetString())
			{
				retVal = entry.path().wstring().c_str();
				break;
			}
		}
	}

	// no file with that extension found, return empty string
	return retVal;
}

// Replace references w/o prompting  user.
BOOL ReplaceReferencesInFile(const WTString& symscope, const WTString& newname, const CStringW& file)
{
	BOOL hasError = FALSE;
	EdCntPtr curEd(g_currentEdCnt);
	FindReferences refs(GetTypeImgIdx(curEd->GetSymDtypeType(), curEd->GetSymDtypeAttrs()), symscope.c_str(),
	                    false, false);
	refs.SearchFile(CStringW(), file);
	size_t count = refs.Count();
	if (!gShellAttr->IsDevenv10OrHigher())
		::SendMessage(MainWndH, WM_SETREDRAW, FALSE, 0);
	for (; count; count--)
	{
		FindReference* ref = refs.GetReference(count - 1);
		if (ref && ref->type != FREF_Unknown && ref->type != FREF_Comment && ref->type != FREF_IncludeDirective &&
		    refs.GotoReference(int(count - 1)))
		{
			WTString sel = curEd->GetSelString();
			if (sel == refs.GetFindSym())
			{
				if (curEd->ReplaceSelW(newname.Wide(), noFormat))
				{
					curEd->SendMessage(WM_COMMAND, WM_VA_REPARSEFILE);
					curEd->OnModified(TRUE);
				}
				else
				{
					hasError = TRUE;
				}
			}
		}
	}
	if (hasError)
	{
		OnRefactorErrorMsgBox();
	}
	if (!gShellAttr->IsDevenv10OrHigher())
	{
		::SendMessage(MainWndH, WM_SETREDRAW, TRUE, 0);
		::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
	return hasError;
}

BEGIN_MESSAGE_MAP(UpdateReferencesDlg, ReferencesWndBase)
//{{AFX_MSG_MAP(FindUsageDlg)
ON_BN_CLICKED(IDC_RENAME, OnUpdate)
ON_BN_CLICKED(IDC_COMMENTS, OnToggleCommentDisplay)
ON_BN_CLICKED(IDC_FIND_INHERITED_REFERENCES, OnToggleWiderScopeDisplay)
ON_BN_CLICKED(IDC_CHK_ALL_PROJECTS, OnToggleAllProjects)
ON_EN_CHANGE(IDC_EDIT1, OnChangeEdit)
ON_COMMAND(ID_CMD_CHECK_ALL, OnCheckAllCmd)
ON_COMMAND(ID_CMD_UNCHECK_ALL, OnUncheckAllCmd)
ON_COMMAND(ID_CMD_CHECK_ALL_COMMENTS, OnCheckAllCommentsAndStrings)
ON_COMMAND(ID_CMD_UNCHECK_ALL_COMMENTS, OnUncheckAllCommentsAndStrings)
ON_COMMAND(ID_CMD_CHECK_ALL_INCLUDES, OnCheckAllIncludes)
ON_COMMAND(ID_CMD_UNCHECK_ALL_INCLUDES, OnUncheckAllIncludes)
// 	ON_COMMAND(ID_CMD_CHECK_ALL_OVERRIDES, OnCheckAllOverridesEtc)
// 	ON_COMMAND(ID_CMD_UNCHECK_ALL_OVERRIDES, OnUncheckAllOverridesEtc)
ON_COMMAND(ID_CMD_CHECK_ALL_UNKNOWN, OnCheckAllUnknown)
ON_COMMAND(ID_CMD_UNCHECK_ALL_UNKNOWN, OnUncheckAllUnknown)
ON_WM_DESTROY()
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void UpdateReferencesDlg::SetSymMaskedType(uint symMaskedType)
{
	mSymMaskedType = symMaskedType;
}

void UpdateReferencesDlg::SetIsUEMarkedType(bool isUEMarkedType)
{
	mIsUEMarkedType = isUEMarkedType;
}

UpdateReferencesDlg::UpdateReferencesDlg(const char* settingsCategory, UINT idd, CWnd* pParent,
                                         bool displayProjectNodes, bool outerUndoContext)
    : ReferencesWndBase(settingsCategory, idd, pParent, displayProjectNodes), mAutoUpdate(false), mColourize(false),
      mIsLongSearch(false), mOuterUndoContext(outerUndoContext)
{
	mFirstVisibleLine = g_currentEdCnt ? g_currentEdCnt->GetFirstVisibleLine() : 0;
	mRefs->ShouldFindAutoVars(false);
}

void UpdateReferencesDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
}

BOOL UpdateReferencesDlg::OnInitDialog()
{
	__super::OnInitDialog(false);
	m_tree.ModifyStyle(0, TVS_CHECKBOXES);

	if (mColourize)
	{
#if 0
		// [case: 82048]
		// Disable until figure out tree control display failure on Thomas' system
		mColourizedEdit = ColourizeControl(this, IDC_EDIT1);
#else
		::mySetProp(GetDlgItem(IDC_EDIT1)->GetSafeHwnd(), "__VA_do_not_colour", (HANDLE)1);
#endif
	}
	else
		::mySetProp(GetDlgItem(IDC_EDIT1)->GetSafeHwnd(), "__VA_do_not_colour", (HANDLE)1);

	if (GetDlgItem(IDC_EDIT1))
	{
		// [case: 9194] do not use DDX_Control due to ColourizeControl.
		// Subclass with colourizer before SHAutoComplete (CtrlBackspaceEdit).
		mEdit_subclassed.SubclassWindow(GetDlgItem(IDC_EDIT1)->m_hWnd);
	}
	mEdit = &mEdit_subclassed;

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_FIND_INHERITED_REFERENCES, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHK_ALL_PROJECTS, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_COMMENTS, this);
		Theme.AddDlgItemForDefaultTheming(IDC_STATUS);
		Theme.AddDlgItemForDefaultTheming(IDC_PROGRESS1);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_RENAME, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	UpdateWordBreakProc(IDC_EDIT1);

	RegisterRenameReferencesControlMovers(); // it was AddSzControl(IDC_RENAME, mdRepos, mdNone);
	AddSzControl(IDC_FIND_INHERITED_REFERENCES, mdNone,
	             mdNone); // tried mdResize but it caused overload with checkbox to right
	AddSzControl(IDC_COMMENTS, mdRelative, mdNone);
	AddSzControl(IDC_CHK_ALL_PROJECTS, mdNone, mdNone);
	{
		EdCntPtr curEd(g_currentEdCnt);
		if (curEd)
			FindCurrentSymbol(m_symScope, GetTypeImgIdx(curEd->GetSymDtypeType(), curEd->GetSymDtypeAttrs()));
	}
	_ASSERTE(mEdit);
	mEdit->SetSel(0, 1000);
	((CButton*)GetDlgItem(IDC_COMMENTS))->SetCheck(Psettings->mDisplayCommentAndStringReferences);
	((CButton*)GetDlgItem(IDC_FIND_INHERITED_REFERENCES))->SetCheck(Psettings->mDisplayWiderScopeReferences);
	((CButton*)GetDlgItem(IDC_CHK_ALL_PROJECTS))->SetCheck(Psettings->mDisplayReferencesFromAllProjects);
	GetDlgItem(IDC_COMMENTS)->EnableWindow(FALSE);
	GetDlgItem(IDC_FIND_INHERITED_REFERENCES)->EnableWindow(FALSE);
	GetDlgItem(IDC_CHK_ALL_PROJECTS)->EnableWindow(FALSE);
	GetDlgItem(IDC_RENAME)->EnableWindow(FALSE);
	::VsScrollbarTheme(m_hWnd);
	mEdit->SetFocus();

	VAUpdateWindowTitle(VAWindowType::UpdateReferences, *this);

	return TRUE;
}

void UpdateReferencesDlg::OnDestroy()
{
	if (mEdit_subclassed.m_hWnd)
		mEdit_subclassed.UnsubclassWindow();

	mEdit = nullptr;

	__super::OnDestroy();
}

void UpdateReferencesDlg::FindCurrentSymbol(const WTString& symScope, int typeImgIdx)
{
	// change IDCANCEL text from Cancel to Stop before starting thread
	if (GetDlgItem(IDCANCEL))
		GetDlgItem(IDCANCEL)->SetWindowText("S&top");
	__super::FindCurrentSymbol(symScope, typeImgIdx);
	_ASSERTE(mEdit_subclassed.GetSafeHwnd());
	mEdit_subclassed.SetText(mEditTxt.Wide());
}

static CStringW sLastFileOpen;

void UpdateReferencesDlg::ShouldNotBeEmpty()
{
	_ASSERTE(mEditTxt.GetLength());
}

void UpdateReferencesDlg::OnUpdate()
{
	mEditTxt.Empty();
	if (mEdit_subclassed.GetSafeHwnd())
	{
		CStringW txt;
		mEdit_subclassed.GetText(txt);
		mEditTxt = txt;
	}
	ShouldNotBeEmpty();
	BOOL hasError = FALSE;

	DWORD renameLimit = Psettings->mRestrictFilesOpenedDuringRefactor;

	if (!ValidateInput())
		return;

	OnCancel();
	if (m_hWnd && IsWindowVisible())
		OnCancel(); // initial onCancel may have been tricked by an unstopped find refs thread

	// if (!OnUpdateStart())
	//	return;

	CWaitCursor cur;
	if (!gShellAttr->IsDevenv10OrHigher())
		::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
	FreezeDisplay _f;

#ifndef RAD_STUDIO
	std::unique_ptr < UndoContext> undoContext;
	if (!mOuterUndoContext)
		undoContext = std::make_unique<UndoContext>("VA Rename");
#endif
	EdCntPtr curEd(g_currentEdCnt);
	const CStringW orgFile = curEd ? curEd->FileName() : CStringW();
	uint orgPos = 0;
	if (curEd)
	{
		// [case: 82391]
		orgPos = curEd->CurPos();
		uint newPos = curEd->WordPos(BEGWORD, orgPos);
		char ch = curEd->CharAt(newPos);
		// when rename is invoked from quick refactor menu, it moves
		// caret to start of word; so do not call WordPos in that case as
		// WordPos will move to start of previous 'word' (ie space).
		if (ISCSYM(ch))
			orgPos = newPos;
	}

	FilesRefItemsCollection filesRefItemsCollection;

#ifdef RAD_STUDIO
	std::vector<DWORD> radDesignerRefs; // actually this is 1 item always
#endif

	// here we clean sLastFileOpen so that it is ready for new iteration
	sLastFileOpen.Empty();

	HTREEITEM baseLevelItem = m_tree.GetRootItem();
	while (baseLevelItem)
	{
#ifdef RAD_STUDIO
		if (IS_FILE_REF_ITEM(m_tree.GetItemData(baseLevelItem)))
		{
			HTREEITEM child = m_tree.GetChildItem(baseLevelItem);
			while (child)
			{
				auto refIdx = (uint)m_tree.GetItemData(child);

				if (refIdx & kRADDesignerRefBit)
				{
					if (m_tree.GetCheck(child))
					{
						radDesignerRefs.emplace_back(refIdx & ~kRADDesignerRefBit);
					}
				}

				child = m_tree.GetNextSiblingItem(child);
			}
		}
#endif
		// get all files and reference items needed to be changed in flat format
		GetFilesAndReferenceItemFlat(baseLevelItem, filesRefItemsCollection);
		baseLevelItem = m_tree.GetNextSiblingItem(baseLevelItem);
	}

	if (filesRefItemsCollection.size() > renameLimit)
	{
		// raise message box notifying that there are more than 50 files and that rename will not be in one run
		WTString message =
		    "Due to the large number of files that will be edited, changes will have to be saved and editors "
		    "closed during the operation causing loss of the ability to use undo.\n\nDo you want to continue?";

		if (IDNO == WtMessageBox(message.Wide(), L"Edit Files?", MB_YESNO | MB_ICONWARNING))
			return;
	}
	if (!OnUpdateStart())
		return;

	size_t numberOfIterations = filesRefItemsCollection.size() / renameLimit;
	if (filesRefItemsCollection.size() % renameLimit)
		numberOfIterations += 1;

#ifdef RAD_STUDIO
	if (!radDesignerRefs.empty() && numberOfIterations == 0)
		numberOfIterations = 1;
#endif

	bool promptForOKToContinue = true;

	// outer loop, it will slice logic in chunks of RENAME_LIMIT
	for (size_t i = 0; i < numberOfIterations; i++)
	{
		for (size_t j = renameLimit * i; j < renameLimit * (i + 1) && j < filesRefItemsCollection.size(); j++)
		{
			// [case: 63374] pre-load files to be modified
			const CStringW curFile(filesRefItemsCollection[j]->first);
			if (!::GetOpenEditWnd(curFile))
				::DelayFileOpen(curFile);
		}

		// go back to original file before starting rename (required for case=63374
		// fix in some cases - .cs and .xaml with .cs active but no change to it)
		::DelayFileOpen(orgFile);

		for (size_t k = renameLimit * i; k < renameLimit * (i + 1) && k < filesRefItemsCollection.size(); k++)
		{
			// update references in opened files
			for (auto& refItem : *filesRefItemsCollection[k]->second)
			{
				int data = (int)m_tree.GetItemData(refItem);
				UpdateResult res = UpdateReference(data, _f);
				if (res == rrError)
					hasError = TRUE;
			}
		}

#ifdef RAD_STUDIO
		// call RAD rename after we done our
		// this is because we don't have to care about new positions
		// but in fact this is not the problem, because we don't replace
		// in header nor in designer file... 
		if (!radDesignerRefs.empty()) // can be empty if unchecked
		{
			for (auto refIdx : radDesignerRefs)
			{
				const FindReference* ref = mRefs->GetReference(refIdx);
				if (ref)
				{
					_ASSERTE(ref->RAD_pass == 1);

					const CStringW curFile(ref->file);
					WTString curSym(mRefs->GetFindSym());
					WTString newSym(mEditTxt);

					if (RAD_DesignerRefsInfo)
					{
						curSym = RAD_passFindSym[ref->RAD_pass - 1];
						RAD_FixReplaceString(ref->RAD_pass, curSym, newSym);						
					}

					gRadStudioHost->RenameDesignerItem((LPCWSTR)curFile, curSym.c_str(), newSym.c_str());
				}
			}
			radDesignerRefs.clear();
		}
#endif

		if (filesRefItemsCollection.size() > renameLimit)
		{
			if (promptForOKToContinue)
			{
				WTString messageContinue =
				    "The first 50 files have been changed but not yet saved. If you continue, files will "
				    "be automatically saved and this prompt will not be shown again during the operation. "
				    "Undo will not be possible after this point.\n\nDo you want to continue?";

				if (IDNO == WtMessageBox(messageContinue.Wide(), L"Edit Files?", MB_YESNO | MB_ICONQUESTION))
					return;

				promptForOKToContinue = false;
			}

			if (gTestsActive && gTestLogger)
			{
				gTestLogger->LogStr(WTString("Skipping SaveAll and CloseAll during AST."));
			}
			else
			{
				::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR) "File.SaveAll", 0);
				::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, (WPARAM)(LPCSTR) "Window.CloseAllDocuments", 0);
			}
		}
	}

	if (hasError)
	{
//#ifndef RAD_STUDIO
		// #RAD_TEMPORARY
		OnRefactorErrorMsgBox();
//#endif
	}

	if (orgFile.GetLength())
	{
		::DelayFileOpen(orgFile);
		_f.LeaveCaretHere();
		curEd = g_currentEdCnt;
		if (curEd)
		{
			if (gShellAttr->IsDevenv())
			{
				// [case: 46551] restore first visible line
				ulong topPos = curEd->LinePos(mFirstVisibleLine);
				if (-1 != topPos)
				{
					curEd->SetSel(topPos, topPos);
					SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("Edit.ScrollLineTop"), 0);
					// don't use gDTE->ExecuteCommand since our dlg proc is running(?)
					// works in vs2010 but not earlier IDEs
				}
			}

			curEd->SetPos(orgPos);
		}
	}

	OnUpdateComplete();
}

void UpdateReferencesDlg::WriteCodeRedirectsForUE(CStringW oldSymName, CStringW newSymName)
{
	// check if new and old sym name are different and rename is really done
	if (oldSymName.Compare(newSymName) != 0)
	{
		bool isPlugin = false;
		CStringW fileNameWithPath = gFileIdManager->GetFile(mRefs->GetSymFileId());

		// first try to find if it is plugin by looking for .uplugin somewhere in parent folders 
		CStringW pathToSearch = fileNameWithPath;
		const int searchStepLimit = 10; // in case of very deep directory tree, we are limiting number of searches
		for (int i = 0; i < searchStepLimit; i++)
		{
			int locationOfBackslash = pathToSearch.ReverseFind(L'\\');
			if (locationOfBackslash > -1)
			{
				// first time remove file name to get path; next remove last folder and go up in hierarchy
				pathToSearch = pathToSearch.Mid(0, locationOfBackslash);
				if (std::filesystem::exists(std::filesystem::path(pathToSearch.GetString()))) // check if folder exists
				{
					// check if *.uplugin file is in folder
					CStringW upluginFile = GetFilePathByExtension(pathToSearch, L".uplugin");
					if (!upluginFile.IsEmpty())
					{
						// there is *.uplugin in the folder; look for .\Config subfolder
						isPlugin = true;
						pathToSearch += L"\\Config";
						if (std::filesystem::exists(std::filesystem::path(pathToSearch.GetString())))
						{
							CStringW iniFile = GetFilePathByExtension(pathToSearch, L".ini");
							if (!iniFile.IsEmpty())
							{
								// INI file exists, no matter how it is called it belong to .uplugin file
								// and the file we are changing
								EditUECoreRedirectsINI(iniFile, oldSymName, newSymName);
							}
							else
							{
								// no INI file in the Config folder, check if there is Tags folder
								pathToSearch += L"\\Tags";
								if (std::filesystem::exists(std::filesystem::path(pathToSearch.GetString())))
								{
									iniFile = GetFilePathByExtension(pathToSearch, L".ini");
									if (!iniFile.IsEmpty())
									{
										// INI file exists in Tags folder
										EditUECoreRedirectsINI(iniFile, oldSymName, newSymName);
									}
								}
							}
						}
						break; // whatever happened here, we are stopping the loop
					}
				}
				else
				{
					// folder does not exist, stop search
					break;
				}
			}
			else
			{
				// no more levels in path, so no point to continue
				break;
			}
		}

		if (!isPlugin)
		{
			// no plugin is detected, try to find DefaultEngine.ini in project file
			CStringW projectNameWithPath = GetProjectName(fileNameWithPath, false);

			// project file is needed to find location of DefaultEngine.ini
			if (!projectNameWithPath.IsEmpty())
			{
				WTString projFileContent;
				projFileContent.ReadFile(projectNameWithPath);
				if (!projFileContent.IsEmpty())
				{
					WTString iniFileName = "DefaultEngine.ini";
					int iniNameLocation = projFileContent.find(iniFileName);
					if (iniNameLocation > -1)
					{
						int iniNameLastChar = iniNameLocation + iniFileName.length();

						// find start of the path which is char after white space
						while (iniNameLocation > 0 && projFileContent[iniNameLocation - 1] != '"')
						{
							iniNameLocation--;
						}

						CStringW coreRedirectsIniFile =
						    projFileContent.substr(iniNameLocation, iniNameLastChar - iniNameLocation).Wide();

						CStringW coreRedirectsIniFileFullPath = coreRedirectsIniFile;
						if (coreRedirectsIniFileFullPath[0] == L'.')
						{
							// ini file path are relative, finding absolute path
							wchar_t pathBuffer[MAX_PATH];
							memset(pathBuffer, 0, MAX_PATH * sizeof(wchar_t));
							int copySize =
							    projectNameWithPath.GetLength() > MAX_PATH ? MAX_PATH : projectNameWithPath.GetLength();
							memcpy(pathBuffer, projectNameWithPath.GetString(), copySize * sizeof(wchar_t));
							::PathRemoveFileSpecW(pathBuffer);
							::PathAppendW(pathBuffer, coreRedirectsIniFile);
							coreRedirectsIniFileFullPath = pathBuffer;
						}

						EditUECoreRedirectsINI(coreRedirectsIniFileFullPath, oldSymName, newSymName);
					}
					else
					{
						// INI file not found, ignore core redirects and just write log
						vLog("WARN: UE Core Redirect not done - failed to find any INI file\n");
					}
				}
			}
			else
			{
				// failed to find project file; file needs to belong to some project either game or engine; if neither,
				// something is wrong so ignore
				vLog("WARN: UE Core Redirect not done - failed to find project file\n");
			}
		}
	}
	else
	{
		// rename called but changed with same name
		vLog("WARN: UE Core Redirect not done - rename called with no change in name\n");
	}
}

void UpdateReferencesDlg::EditUECoreRedirectsINI(const CStringW& coreRedirectsIniFileFullPath, CStringW& oldSymName,
                                                 CStringW& newSymName)
{
	// if INI file can be found, proceed with adding core redirect
	if (std::filesystem::exists(std::filesystem::path(coreRedirectsIniFileFullPath.GetString())))
	{
		EdCntPtr curEd(g_currentEdCnt);
		const CStringW orgFile = curEd ? curEd->FileName() : CStringW();
		
		gIgnoreRestrictedFileType = TRUE;	// .ini is text file; ignore restriction of file type
		auto edCntPtr = ::GetOpenEditWnd(coreRedirectsIniFileFullPath);
		if (!edCntPtr)
			edCntPtr = ::DelayFileOpen(coreRedirectsIniFileFullPath);
		gIgnoreRestrictedFileType = FALSE; // remove ignore of restriction

		bool writeCodeRedirect = false;
		bool trimFirstLetter = true; // false for enums, otherwise true
		CStringW redirectsString = "";

		switch (mSymMaskedType)
		{
		case CLASS:
			redirectsString = L"+ClassRedirects";
			writeCodeRedirect = true;
			break;

		case STRUCT:
			redirectsString = L"+StructRedirects";
			writeCodeRedirect = true;
			break;

		case FUNC:
			redirectsString = L"+FunctionRedirects";
			trimFirstLetter = false;
			writeCodeRedirect = true;
			break;

		case C_ENUM:
			redirectsString = L"+EnumRedirects";
			trimFirstLetter = false;
			writeCodeRedirect = true;
			break;

		default:
			writeCodeRedirect = false;
			break;
		}

		if (writeCodeRedirect)
		{
			if (trimFirstLetter && oldSymName.GetLength() > 1 && newSymName.GetLength() > 1)
			{
				// in case of class or struct, prefix letter should be removed
				// the name should be written as it appears to the Unreal Engine's reflection system
				// class can start with A or U while struct starts with F; remove those letters if found
				if ((mSymMaskedType == CLASS && ((oldSymName[0] == L'U' && newSymName[0] == L'U') ||
				                                 (oldSymName[0] == L'A' && newSymName[0] == L'A'))) ||
				    (mSymMaskedType == STRUCT && oldSymName[0] == L'F' && newSymName[0] == L'F'))
				{
					oldSymName.Delete(0, 1);
					newSymName.Delete(0, 1);
				}
			}

			// build redirects value string, e.g.
			// +ClassRedirects=(OldName="Pawn",NewName="MyPawn")
			redirectsString.Append(LR"(=(OldName=")");
			redirectsString.Append(oldSymName);
			redirectsString.Append(LR"(",NewName=")");
			redirectsString.Append(newSymName);
			redirectsString.Append(LR"("))");

			CStringW sectionName = L"[CoreRedirects]";

			WTString fileBuf;
			
			// get file content either through Editor or from file
			if (edCntPtr)
			{
				// read INI file in VS editor
				fileBuf = edCntPtr->GetBuf();
			}
			else
			{
				// we are not able to hook to the Editor, so take file directly from disk
				vLog("WARN: UE Core Redirect - failed to open INI file in edtior; manipulate file directly on disk\n");
				
				if(!fileBuf.ReadFile(coreRedirectsIniFileFullPath))
				{
					vLog("WARN: UE Core Redirect - failed to read file on disk\n");
					::DelayFileOpen(orgFile); // go back to original file
					return;
				}
			}

			// check if this core redirect already exists in the file
			if (fileBuf.Find(redirectsString) > -1)
			{
				vLog("INFO: UE Core Redirect - entry already exists\n");
				::DelayFileOpen(orgFile); // go back to original file
				return;
			}
			
			// construct text to insert into file
			const CStringW newLineChar = fileBuf.find(L"\r\n") > -1 ? L"\r\n" : L"\n"; // use the same as in ini file
			uint insertLocation = (uint)fileBuf.length(); // this is initially set to the end of the file
			CStringW insertText;
			int sectionLocation = fileBuf.FindNoCase(sectionName);
			if (sectionLocation > -1)
			{
				// section [CoreRedirects] exists, put line to the end of it before next section or end of file if next section doesn't exist
				for (int i = sectionLocation + sectionName.GetLength(); i < fileBuf.length(); i++)
				{
					if (fileBuf[i] == L'[')
					{
						// found next section, insert core redirect before it
						insertLocation = (uint)i;
						break;
					}
				}

				insertText = newLineChar + redirectsString;
			}
			else
			{
				// section does not exist, create it an add on the end of file
				insertText = newLineChar + newLineChar + sectionName + newLineChar + redirectsString;
			}

			// find last non white space char to put text on the correct place ant to avoid multiple empty lines
			for (/**/; insertLocation > 0; insertLocation--)
			{
				auto prevChar = fileBuf[insertLocation - 1];
				if (prevChar != L'\n' && prevChar != L'\r' && prevChar != L' ' && prevChar != L'\t')
				{
					// prev char is not white space, so we are at correct place
					break;
				}
			}

			// change file content either through Editor or from file
			if (edCntPtr)
			{
				// insert into INI file in VS Editor
				edCntPtr->SetPos(insertLocation);
				edCntPtr->InsertW(insertText);
			}
			else
			{
				// compose content and write into file directly on disk
				fileBuf.insert((int)insertLocation, CString(insertText));

				::SetFileAttributesW(coreRedirectsIniFileFullPath,
				                   ::GetFileAttributesW(coreRedirectsIniFileFullPath) & ~FILE_ATTRIBUTE_READONLY);
				std::ofstream iniFile(coreRedirectsIniFileFullPath, std::ios::binary | std::ios::trunc);
				if (iniFile.is_open())
				{
					iniFile << fileBuf.GetBuffer(fileBuf.GetLength());
					iniFile.close();
				}
				else
				{
					// write to ini file failed
					vLog("WARN: UE Core Redirect - failed to write file on disk\n");
				}
			}
		}

		::DelayFileOpen(orgFile); // go back to original file
	}
	else
	{
		// failed to find INI file
		vLog("WARN: UE Core Redirect not done - INI file not found on location\n");
	}
}

void UpdateReferencesDlg::OpenFilesForUpdate(HTREEITEM refItem)
{
	if (IS_FILE_REF_ITEM(m_tree.GetItemData(refItem)))
	{
		HTREEITEM child = m_tree.GetChildItem(refItem);
		while (child)
		{
			OpenFilesForUpdate(child);
			if (IS_FILE_REF_ITEM(m_tree.GetItemData(child)))
				child = m_tree.GetNextSiblingItem(child);
			else
				break;
		}
		return;
	}

	while (refItem)
	{
		if (m_tree.GetCheck(refItem))
		{
			int refIdx = (int)m_tree.GetItemData(refItem);
			if (!(IS_FILE_REF_ITEM(refIdx)))
			{
				const FindReference* ref = mRefs->GetReference((uint)refIdx);
				if (ref)
				{
					const CStringW curFile(ref->file);
					if (curFile != sLastFileOpen)
					{
						sLastFileOpen = curFile;
						if (!::GetOpenEditWnd(curFile))
							::DelayFileOpen(curFile);
					}
				}
			}
		}
		refItem = m_tree.GetNextSiblingItem(refItem);
	}
}

void UpdateReferencesDlg::UpdateReferences(HTREEITEM refItem, FreezeDisplay& _f, int& nChanged, BOOL& hasError)
{
	if (IS_FILE_REF_ITEM(m_tree.GetItemData(refItem)))
	{
		HTREEITEM child = m_tree.GetChildItem(refItem);
		while (child)
		{
			UpdateReferences(child, _f, nChanged, hasError);
			if (IS_FILE_REF_ITEM(m_tree.GetItemData(child)))
				child = m_tree.GetNextSiblingItem(child);
			else
				break;
		}
		return;
	}

	// get last reference item
	while (refItem && m_tree.GetNextSiblingItem(refItem))
		refItem = m_tree.GetNextSiblingItem(refItem);

	// replace from bottom to top of file so offsets align
	while (refItem)
	{
		if (m_tree.GetCheck(refItem))
		{
			int data = (int)m_tree.GetItemData(refItem);
			UpdateResult res = UpdateReference(data, _f);
			switch (res)
			{
			case rrSuccess:
				++nChanged;
				break;
			case rrError:
				hasError = TRUE;
				break;
			default:
				break;
			}
		}
		refItem = m_tree.GetPrevSiblingItem(refItem);
	}
}

void UpdateReferencesDlg::OnSearchBegin()
{
	mIsLongSearch = false;
	__super::OnSearchBegin();

	CWnd* cancel_wnd = GetDlgItem(IDCANCEL);
	if (cancel_wnd)
	{
		HWND cancel_hWnd = cancel_wnd->m_hWnd;

		// wait for a while and change text of button,
		// user now can click Cancel to close the dialog
		DelayedInvoke(100, [this, cancel_hWnd]() {
			// delayed because it was causing behavior similar to flicker when there
			// was quick reaction from search thread. It looked like bug as it
			// twice changed text very quickly. When that lambda is called,
			// it checks if mFindRefsThread->IsRunning() and if so, then text
			// of button is changed.  That prevents from blink.
			if (::IsWindow(cancel_hWnd) && mFindRefsThread && mFindRefsThread->IsRunning())
			{
				mIsLongSearch = true;

				// change IDCANCEL text from Cancel to Stop
				::SetWindowTextA(cancel_hWnd, "S&top");

				// ensure that Stop button is enabled
				::EnableWindow(cancel_hWnd, TRUE);
			}
		});
	}
}

void UpdateReferencesDlg::OnSearchComplete(int fileCount, bool wasCanceled)
{
	__super::OnSearchComplete(fileCount, wasCanceled);

	GetDlgItem(IDC_COMMENTS)->EnableWindow(TRUE);
	GetDlgItem(IDC_FIND_INHERITED_REFERENCES)->EnableWindow(TRUE);
	GetDlgItem(IDC_CHK_ALL_PROJECTS)->EnableWindow(TRUE);

	// allow rename even if user stopped find refs thread
	// prematurely unless this was an autoRename
	// change IDCANCEL text from Stop back to Cancel now that the thread is finished
	CWnd* cancel_wnd = GetDlgItem(IDCANCEL);
	if (cancel_wnd && GetWindowTextString(cancel_wnd->m_hWnd) == "S&top")
	{
		if (mIsLongSearch)
		{
			// disable Stop button to disallow user to mistakenly press it.
			cancel_wnd->EnableWindow(FALSE);
		}

		// passing HWND, because CWnd could be deleted at the moment of invoke
		HWND wnd_hWhd = cancel_wnd->m_hWnd;

		auto ResetButton = [wnd_hWhd]() {
			if (::IsWindow(wnd_hWhd))
			{
				::SetWindowTextA(wnd_hWhd, "&Cancel");
				::EnableWindow(wnd_hWhd, TRUE);
			}
		};

		if (mIsLongSearch)
		{
			// wait for a while and change text of button,
			// user now can click Cancel to close the dialog
			DelayedInvoke(500, ResetButton);
		}
		else
		{
			// change immediately
			ResetButton();
		}
	}
	if (GetDlgItem(IDC_RENAME))
		GetDlgItem(IDC_RENAME)->EnableWindow(ValidateInput());
	if (!wasCanceled && mAutoUpdate)
		PostMessage(WM_COMMAND, IDC_RENAME, 0);

	InspectContents(TVI_ROOT);
}

void UpdateReferencesDlg::OnCancel()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
	{
		if (mFindRefsThread->IsStopped())
		{
			// already tried to cancel but thread is still running...
			__super::OnCancel();
			return;
		}

		mFindRefsThread->Cancel();
		// change IDCANCEL text from Stop to Cancel
		if (GetDlgItem(IDCANCEL))
			GetDlgItem(IDCANCEL)->SetWindowText("&Cancel");
	}
	else
		__super::OnCancel();

	GetDlgItem(IDC_COMMENTS)->EnableWindow(TRUE);
	GetDlgItem(IDC_FIND_INHERITED_REFERENCES)->EnableWindow(TRUE);
	GetDlgItem(IDC_CHK_ALL_PROJECTS)->EnableWindow(TRUE);
}

void UpdateReferencesDlg::OnDoubleClickTree()
{
	__super::OnDoubleClickTree();
	SetFocus();
}

void UpdateReferencesDlg::CheckDescendants(HTREEITEM item, int refType, bool check)
{
	CWaitCursor curs;
	HTREEITEM childItem = m_tree.GetChildItem(item);
	while (childItem)
	{
		uint data = (uint)m_tree.GetItemData(childItem);
		if (!IS_FILE_REF_ITEM(data))
		{
			FindReference* ref = mRefs->GetReference(data);
			if (ref)
			{
				if (FREF_JsSameName == refType)
				{
					if (ref->overridden)
						m_tree.SetCheck(childItem, check);
				}
				else if (ref->type == refType)
					m_tree.SetCheck(childItem, check);
			}
		}
		if (m_tree.ItemHasChildren(childItem))
			CheckDescendants(childItem, refType, check);
		childItem = m_tree.GetNextSiblingItem(childItem);
	}
}

void UpdateReferencesDlg::CheckDescendants(HTREEITEM item, bool check)
{
	HTREEITEM childItem = m_tree.GetChildItem(item);
	while (childItem)
	{
		m_tree.SetCheck(childItem, check);

		if (m_tree.ItemHasChildren(childItem))
			CheckDescendants(childItem, check);

		childItem = m_tree.GetNextSiblingItem(childItem);
	}
}

void UpdateReferencesDlg::OnToggleCommentDisplay()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
	{
		_ASSERTE(!"findRefsThread still running");
		return;
	}

	CWaitCursor curs;
	__super::OnToggleFilterComments();
}

void UpdateReferencesDlg::CheckAllCommentsAndStrings(bool check)
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return;

	Psettings->mRenameCommentAndStringReferences = check;
	CheckDescendants(TVI_ROOT, FREF_Comment, Psettings->mRenameCommentAndStringReferences);
}

void UpdateReferencesDlg::OnCheckAllCommentsAndStrings()
{
	CheckAllCommentsAndStrings(true);
}

void UpdateReferencesDlg::OnUncheckAllCommentsAndStrings()
{
	CheckAllCommentsAndStrings(false);
}

void UpdateReferencesDlg::OnToggleWiderScopeDisplay()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
	{
		_ASSERTE(!"findRefsThread still running");
		return;
	}

	CWaitCursor curs;
	__super::OnToggleFilterInherited();
}

void UpdateReferencesDlg::OnToggleAllProjects()
{
	if (mFindRefsThread && mFindRefsThread->IsRunning())
	{
		_ASSERTE(!"findRefsThread still running");
		return;
	}

	bool isChecked = ((CButton*)GetDlgItem(IDC_CHK_ALL_PROJECTS))->GetCheck() == BST_CHECKED;
	Psettings->mDisplayReferencesFromAllProjects = isChecked;

	CWaitCursor curs;
	OnRefresh();
}

void UpdateReferencesDlg::OnToggleCoreRedirects()
{
	mCreateCoreRedirects = ((CButton*)GetDlgItem(IDC_CHK_CORE_REDIRECTS))->GetCheck() == BST_CHECKED;
}

// this does not do what you think it does...
// void
// UpdateReferencesDlg::OnCheckAllOverridesEtc()
// {
// 	CheckAllOverridesEtc(true);
// }
//
// void
// UpdateReferencesDlg::OnUncheckAllOverridesEtc()
// {
// 	CheckAllOverridesEtc(false);
// }
//
// void
// UpdateReferencesDlg::CheckAllOverridesEtc(bool check)
// {
// 	if (mFindRefsThread && mFindRefsThread->IsRunning())
// 		return;
//
// 	Psettings->mRenameWiderScopeReferences = check;
// 	CheckDescendants(TVI_ROOT, FREF_JsSameName, Psettings->mRenameWiderScopeReferences);
// }

void UpdateReferencesDlg::OnPopulateContextMenu(CMenu& contextMenu)
{
	const bool threadActive = mFindRefsThread && mFindRefsThread->IsRunning();
	if (!threadActive)
	{
		contextMenu.AppendMenu(0, ID_CMD_CHECK_ALL, "&Check all");
		contextMenu.AppendMenu(0, ID_CMD_UNCHECK_ALL, "&Uncheck all");

		// this does not do what you think it does...
		// 		if (mHasOverrides)
		// 		{
		// 			contextMenu.AppendMenu(0, ID_CMD_CHECK_ALL_OVERRIDES,	"Check &inherited and overridden");
		// 			contextMenu.AppendMenu(0, ID_CMD_UNCHECK_ALL_OVERRIDES,	"Uncheck inherited and &overridden");
		// 		}

		if (mRefTypes[FREF_Comment])
		{
			contextMenu.AppendMenu(0, ID_CMD_CHECK_ALL_COMMENTS, "Check co&mments and strings");
			contextMenu.AppendMenu(0, ID_CMD_UNCHECK_ALL_COMMENTS, "Uncheck comments and &strings");
		}

		if (mRefTypes[FREF_IncludeDirective])
		{
			contextMenu.AppendMenu(0, ID_CMD_CHECK_ALL_INCLUDES, "Check i&ncludes");
			contextMenu.AppendMenu(0, ID_CMD_UNCHECK_ALL_INCLUDES, "Uncheck inc&ludes");
		}

		if (mRefTypes[FREF_Unknown])
		{
			contextMenu.AppendMenu(0, ID_CMD_CHECK_ALL_UNKNOWN, "Check un&knowns");
			contextMenu.AppendMenu(0, ID_CMD_UNCHECK_ALL_UNKNOWN, "Uncheck unkno&wns");
		}
	}
}

void UpdateReferencesDlg::OnCheckAllCmd()
{
	CheckDescendants(TVI_ROOT, true);
}

void UpdateReferencesDlg::OnUncheckAllCmd()
{
	CheckDescendants(TVI_ROOT, false);
}

void UpdateReferencesDlg::OnCheckAllIncludes()
{
	CheckDescendants(TVI_ROOT, FREF_IncludeDirective, true);
}

void UpdateReferencesDlg::OnUncheckAllIncludes()
{
	CheckDescendants(TVI_ROOT, FREF_IncludeDirective, false);
}

void UpdateReferencesDlg::OnCheckAllUnknown()
{
	CheckDescendants(TVI_ROOT, FREF_Unknown, true);
}

void UpdateReferencesDlg::OnUncheckAllUnknown()
{
	CheckDescendants(TVI_ROOT, FREF_Unknown, false);
}

void UpdateReferencesDlg::OnChangeEdit()
{
	// get the latest text even when thread running
	if (mEdit_subclassed.GetSafeHwnd())
	{
		CStringW txt;
		mEdit_subclassed.GetText(txt);
		mEditTxt = txt;
	}

	if (mFindRefsThread && mFindRefsThread->IsRunning())
		return; // don't refresh while thread is still running

	BOOL ok = ValidateInput();

	CButton* pRenameBtn = (CButton*)GetDlgItem(IDC_RENAME);
	if (pRenameBtn)
		pRenameBtn->EnableWindow(ok);
}

void UpdateReferencesDlg::RegisterRenameReferencesControlMovers()
{
	AddSzControl(IDC_EDIT1, mdResize, mdNone);
	AddSzControl(IDC_RENAME, mdRepos, mdNone);
}

void UpdateReferencesDlg::GetFilesAndReferenceItemFlat(HTREEITEM refItem,
                                                       FilesRefItemsCollection& filesRefItemsCollection)
{
	if (IS_FILE_REF_ITEM(m_tree.GetItemData(refItem)))
	{
		uint refIdx = 0;
		HTREEITEM child = m_tree.GetChildItem(refItem);
		while (child)
		{
			refIdx = (uint)m_tree.GetItemData(child);
		
#ifdef RAD_STUDIO			
			if (refIdx & kRADDesignerRefBit)
			{
				child = m_tree.GetNextSiblingItem(child);
				continue;
			}
#endif

			GetFilesAndReferenceItemFlat(child, filesRefItemsCollection);

			if (IS_FILE_REF_ITEM(refIdx))
				child = m_tree.GetNextSiblingItem(child);
			else
				break;
		}
		return;
	}

	while (refItem)
	{
		if (m_tree.GetCheck(refItem))
		{
			auto refIdx = (uint)m_tree.GetItemData(refItem);
			bool isSymbolRef = !(IS_FILE_REF_ITEM(refIdx));

#ifdef RAD_STUDIO
			if (refIdx & kRADDesignerRefBit)
				isSymbolRef = false;
#endif

			if (isSymbolRef)
			{
				const FindReference* ref = mRefs->GetReference(refIdx);
				if (ref)
				{
#ifdef RAD_STUDIO
					const CStringW curFile(NormalizeFilepath(ref->file).MakeUpper());
#else
					const CStringW curFile(ref->file);
#endif // RAD_STUDIO

					if (curFile != sLastFileOpen)
					{
						sLastFileOpen = curFile;
						std::shared_ptr<std::deque<HTREEITEM>> pReferenceItemDeque =
						    std::make_shared<std::deque<HTREEITEM>>();
						pReferenceItemDeque->push_front(refItem);

						std::shared_ptr<FileRefItemsElement> pFileRefItemsElement =
						    std::make_shared<FileRefItemsElement>(curFile, pReferenceItemDeque);
						filesRefItemsCollection.push_back(pFileRefItemsElement);
					}
					else
					{
						// using deque and push_front to achieve replace from bottom to top of file so offsets align
						filesRefItemsCollection.back()->second->push_front(refItem);
					}
				}
			}
		}
		refItem = m_tree.GetNextSiblingItem(refItem);
	}
}

// rename references ctor - saves position
RenameReferencesDlg::RenameReferencesDlg(const WTString& symScope, LPCSTR newName /* = NULL */,
                                         BOOL autoRename /*= FALSE*/, BOOL ueRenameImplicit /*= FALSE*/)
    : UpdateReferencesDlg("RenameDlg", IDD, NULL, Psettings->mIncludeProjectNodeInRenameResults, false)
{
	SetHelpTopic("dlgRenameReferences");

	if (!symScope.IsEmpty())
		m_symScope = symScope;
	else if (g_currentEdCnt)
		m_symScope = g_currentEdCnt->GetSymScope();
	mEditTxt = (newName && newName[0]) ? newName : StrGetSym(m_symScope);
	if (newName && autoRename)
		mAutoUpdate = TRUE;
	if (ueRenameImplicit)
	{
		// [case: 141287] find *_Implementaiton and *_Validate methods so they can be renamed as well
		mRefs->flags |= FREF_Flg_UeFindImplicit;
	}
}

BOOL RenameReferencesDlg::OnInitDialog()
{
	__super::OnInitDialog();

	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddThemedSubclasserForDlgItem<CThemedCheckBox>(IDC_CHK_CORE_REDIRECTS, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	AddSzControl(IDC_CHK_CORE_REDIRECTS, mdRelative, mdNone);
	GetDlgItem(IDC_CHK_CORE_REDIRECTS)->ShowWindow(Psettings->mUnrealEngineCppSupport && mIsUEMarkedType); // show option only if it is UE solution and marked type
	((CButton*)GetDlgItem(IDC_CHK_CORE_REDIRECTS))->SetCheck(false);

	VAUpdateWindowTitle(VAWindowType::RenameReferences, *this);

	return FALSE;
}

void RenameReferencesDlg::OnSearchBegin()
{
#if defined(RAD_STUDIO)
	RAD_passFindSym[mRefs->RAD_pass] = mRefs->GetFindSym();
	mRefs->RAD_pass++;
#endif

	GetDlgItem(IDC_COMMENTS)->EnableWindow(FALSE);
	GetDlgItem(IDC_FIND_INHERITED_REFERENCES)->EnableWindow(FALSE);
	GetDlgItem(IDC_CHK_ALL_PROJECTS)->EnableWindow(FALSE);
	GetDlgItem(IDC_RENAME)->EnableWindow(FALSE);
	__super::OnSearchBegin();
}

void RenameReferencesDlg::OnSearchComplete(int fileCount, bool wasCanceled)
{
#if defined(RAD_STUDIO)
	if (!RAD_fieldPassSymScope.IsEmpty() && mRefs->RAD_pass == 1)
	{
		if (RAD_FindNextSymbol(RAD_fieldPassSymScope, RAD_fieldPassRefImgIdx, mIsLongSearch ? 500u : 0u))
			return;
	}

	if (RAD_DesignerRefsInfo)
		RAD_DesignerRefsInfo->SetRefsScopeInfo(*mRefs);

	__super::OnSearchProgress(100, 0);
#endif

	if (gTestLogger && gTestLogger->IsDialogLoggingEnabled())
	{
		try
		{
			WTString logStr;
			logStr.WTFormat("Rename results from(%s) to(%s) auto(%d) fileCnt(%d) refCnt(%zu) canceled(%d)\r\n",
			                m_symScope.c_str(), mEditTxt.c_str(), mAutoUpdate, fileCount, mRefs->Count(), wasCanceled);
			gTestLogger->LogStr(logStr);
			gTestLogger->LogStrW(mRefs->GetSummary());
		}
		catch (...)
		{
			gTestLogger->LogStr(WTString("Exception logging Rename results."));
		}
	}

	GetDlgItem(IDC_COMMENTS)->EnableWindow(TRUE);
	GetDlgItem(IDC_FIND_INHERITED_REFERENCES)->EnableWindow(TRUE);
	GetDlgItem(IDC_CHK_ALL_PROJECTS)->EnableWindow(TRUE);
	GetDlgItem(IDC_RENAME)->EnableWindow(TRUE);
	mEdit->SetFocus();
	__super::OnSearchComplete(fileCount, wasCanceled);
}

void RenameReferencesDlg::UpdateStatus(BOOL done, int fileCount)
{
	WTString msg;
	if (!done)
		msg.WTFormat("Searching %s...", mRefs->GetScopeOfSearchStr().c_str());
	else if (mFindRefsThread && mFindRefsThread->IsStopped())
		msg.WTFormat("Search canceled before completion.  Re&name %s at your own risk to:",
		             mRefs->GetFindSym().c_str());
	else
		msg.WTFormat("Re&name %s (in %s) to:", mRefs->GetFindSym().c_str(), mRefs->GetScopeOfSearchStr().c_str());

	::SetWindowTextW(GetDlgItem(IDC_STATUS)->GetSafeHwnd(), msg.Wide());
}

UpdateReferencesDlg::UpdateResult RenameReferencesDlg::UpdateReference(int refIdx, FreezeDisplay& _f)
{
	if (IS_FILE_REF_ITEM(refIdx))
		return rrNoChange;

	auto fref = mRefs->GetReference((size_t)refIdx);
	if (!fref)
		return rrNoChange;

#if defined(RAD_STUDIO)
	auto findText = RAD_passFindSym[fref->RAD_pass - 1];
#else
	auto findText = mRefs->GetFindSym();
#endif

	if (mRefs->GotoReference(refIdx, false, findText.Wide().GetLength()))
	{
		EdCntPtr curEd(g_currentEdCnt);
		if (curEd)
		{
			WTString sel = curEd->GetSelString();
			bool matchFound = false;
			enum class UeMatchType
			{
				Undefined,
				NoThunk,
				Implementation,
				Validate
			} ueMatchType = UeMatchType::Undefined;

			WTString replText = mEditTxt;
			
#if defined(RAD_STUDIO)
			RAD_FixReplaceString(fref->RAD_pass, findText, replText);
#endif

			if (mRefs->flags & FREF_Flg_UeFindImplicit)
			{
				// [case: 141287] update *_Implementaiton and *_Validate methods
				if (sel == findText)
					ueMatchType = UeMatchType::NoThunk;
				else if (sel == findText + "_Implementation")
					ueMatchType = UeMatchType::Implementation;
				else if (sel == findText + "_Validate")
					ueMatchType = UeMatchType::Validate;
				matchFound = ueMatchType != UeMatchType::Undefined ? true : false;
			}
			else
			{
				matchFound = sel == findText;
			}

			if (matchFound)
			{
				_f.ReadOnlyCheck();
				BOOL replaceRslt = FALSE;
				if (ueMatchType == UeMatchType::Undefined)
				{
					replaceRslt = curEd->ReplaceSelW(replText.Wide(), noFormat);
				}
				else
				{
					WTString modifiedSymName = replText;
					switch (ueMatchType)
					{
					case UeMatchType::Implementation:
						modifiedSymName += "_Implementation";
						break;
					case UeMatchType::Validate:
						modifiedSymName += "_Validate";
						break;
					default:
						break;
					}
					replaceRslt = curEd->ReplaceSelW(modifiedSymName.Wide(), noFormat);
				}

				if (replaceRslt)
				{
					if (gShellAttr->IsMsdev())
						curEd->GetBuf(TRUE);

					curEd->OnModified(TRUE);
					return rrSuccess;
				}
			}
		}
	}
	return rrError;
}

CStringW GetProjectName(CStringW fileName, bool noPath)	// noPath = true from forward declaration
{
	fileName.MakeUpper();
	RWLockReader lck;
	const Project::ProjectMap& projMap = GlobalProject->GetProjectsForRead(lck);
	for (Project::ProjectMap::const_iterator projIt = projMap.begin(); projIt != projMap.end(); ++projIt)
	{
		const ProjectInfoPtr projInf = (*projIt).second;
		if (projInf)
		{
			CStringW projectFile(projInf->GetProjectFile());
			CStringW currentProject(noPath ? ::GetBaseNameNoExt(projectFile) : projectFile);

			const FileList& projFiles = projInf->GetFiles();
			for (FileList::const_iterator filesIt = projFiles.begin(); filesIt != projFiles.end(); ++filesIt)
			{
				CStringW projFileName = filesIt->mFilename;
				projFileName.MakeUpper();
				if (projFileName == fileName) // there was an example, where the directory case was different here and
				                              // there for some reason...
				{
					if (projInf->IsDeferredProject())
					{
						vLog("WARN: RenameReferences GetProjectName found deferred\n");
						return L"";
					}

					if (projInf->IsPseudoProject())
					{
						vLog("WARN: RenameReferences GetProjectName found pseudo\n");
						return L"";
					}

					return currentProject;
				}
			}
		}
		else
		{
			_ASSERTE(!"project map iterator has NULL proj info");
			vLog("ERROR: GetProjectName null info\n");
		}
	}

	return L"";
}

bool ShouldAllowRenameFiles(FindReferences::FindRefsSearchScope scope, CStringW refFileName, CStringW defFileName)
{
	if (!gShellAttr->IsDevenv8OrHigher())
		return false; // per VARefactorCls::CanRenameFiles

	if (scope == FindReferences::searchSolution)
		return true;

	CStringW refProjectName = GetProjectName(refFileName);
	if (refProjectName.IsEmpty())
		return false;

	CStringW defProjectName = GetProjectName(defFileName);
	if (defProjectName.IsEmpty())
		return false;

	return refProjectName == defProjectName;
}

void RenameReferencesDlg::OnUpdateComplete()
{
	// [case: 147774] handle unreal engine core redirects
	if (Psettings->mUnrealEngineCppSupport && mCreateCoreRedirects && mIsUEMarkedType)
	{
		try
		{
			WriteCodeRedirectsForUE(mRefs->GetFindSym().Wide(), mEditTxt.Wide());
		}
		catch (...)
		{
			// this should not happen but if it is, we will just not write core redirect
			vLog("ERROR: exception caught while writing core redirects during rename reference\n");
		}
	}
	
	mRefs->UpdateSymForRename(mEditTxt, StrGetSymScope(mRefs->GetFindScope() + ':' + mEditTxt.c_str()));

	CStringW fileNameWithPath = gFileIdManager->GetFile(mRefs->GetSymFileId());
	FindReferences::FindRefsSearchScope scope = mRefs->GetScopeOfSearch();
	EdCntPtr curEd(g_currentEdCnt);
	if (curEd && ShouldAllowRenameFiles(scope, curEd->FileName(), fileNameWithPath))
	{
		CStringW oldSymbolName = mRefs->GetRenamedSym().Wide();
		CStringW fileName = ::Basename(fileNameWithPath);
		CStringW fileBase = ::GetBaseNameNoExt(fileName);
		const WTString& newSymbolName = mRefs->GetFindSym();
		oldSymbolName.MakeUpper();
		fileBase.MakeUpper();
		if (oldSymbolName == fileBase)
		{
			WTString message;
			message.WTFormat("The renamed symbol was declared in a file of the same name (%s).\n\n",
			                 WTString(fileName).c_str());
			message += "Do you want to save all files and run Rename Files?";
			if (gTestsActive && gTestLogger)
				gTestLogger->LogStr(message);
			if (IDYES == WtMessageBox(message.Wide(), L"Rename Files?", MB_YESNO | MB_ICONQUESTION))
			{
				curEd->SendVamMessage(VAM_EXECUTECOMMAND, (WPARAM) _T("File.SaveAll"), 0);

				DelayFileOpen(fileNameWithPath, 0, NULL);

				Refactor(VARef_RenameFilesFromRenameRefs, nullptr, newSymbolName);
			}
		}
	}
}

BOOL RenameReferencesDlg::ValidateInput()
{
	WTString trimmed = mEditTxt;
	trimmed.TrimLeft();
	if (trimmed.IsEmpty())
	{
		return FALSE;
	}

	return TRUE;
}

#if defined(RAD_STUDIO)
// RAD Specific implementation!!!
LRESULT RenameReferencesDlg::OnAddReference(WPARAM wparam, LPARAM lparam)
{
	if (!RAD_skipSymbols)
		return __super::OnAddReference(wparam, lparam);
	
	return 0;
}

// RAD Specific implementation!!!
LRESULT RenameReferencesDlg::OnAddFileReference(WPARAM wparam, LPARAM lparam)
{
	const TreeReferenceInfo* info = reinterpret_cast<TreeReferenceInfo*>(wparam);
	RAD_skipSymbols = false;

	if (RAD_designerFiles.empty())
	{
		CStringW srcFile, hdrFile;
		auto fileType = GetFileType(info->mText);

		// if file is not header nor source, skip it (perhaps not possible in RAD)
		if (fileType != Src && fileType != Header)
			return RAD_FindOrAddFileReference(wparam, lparam);

		if (fileType == Src)
		{
			// source is known, we need to find header
			srcFile = info->mText;
			hdrFile = srcFile;

			if (!RadUtils::SwapFileExtension(hdrFile))
				return RAD_FindOrAddFileReference(wparam, lparam);
		}
		else
		{
			// header is known, we need to find source
			hdrFile = info->mText;
			srcFile = hdrFile;

			if (!RadUtils::SwapFileExtension(srcFile))
				return RAD_FindOrAddFileReference(wparam, lparam);
		}

		// let's check if these files require designer changes
		if (!srcFile.IsEmpty() && !hdrFile.IsEmpty())
		{
			WTString findSym(mRefs->GetFindSym());
			int symCount = gRadStudioHost->NeedToUpdateDesigner(srcFile, findSym.c_str());
			if (symCount)
			{
				RAD_AddDesignerItem(info);

				// assign files so next time we don't have to do this again
				RAD_designerFiles.emplace(gFileIdManager->GetFileId(srcFile));
				RAD_designerFiles.emplace(gFileIdManager->GetFileId(hdrFile));

				RAD_skipSymbols = true;	// we skip symbols in header files
				
				if (symCount == 2)
				{
					EdCntPtr ed = g_currentEdCnt;
					if (ed && !findSym.IsEmpty() && findSym[0] == 'T')
					{
						auto mp = g_currentEdCnt->GetParseDb();
						WTString field = findSym.substr(1);
						DTypeList dtypes;
						int found = mp->FindExactList(field, dtypes, false);
						if (found)
						{
							for (DType& cur : dtypes)
							{
								WTString def = cur.Def();
								if (strstrWholeWord(def, findSym))
								{
									RAD_DesignerRefsInfo = std::make_unique<FindRefsScopeInfo>(*mRefs);
									RAD_fieldPassSymScope = cur.SymScope();
									RAD_fieldPassRefImgIdx = VAR;
									break;
								}
							}
						}
					}
				}

				if (GetFileType(info->mText) == Src)
				{
					RAD_skipSymbols = false;
					return RAD_FindOrAddFileReference(wparam, lparam);
				}

				return 0;
			}
		}
	}
	else
	{
		// check if file is known designer file
		auto id = gFileIdManager->GetFileId(info->mText);
		if (RAD_designerFiles.contains(id))
		{
			RAD_skipSymbols = true; // we skip symbols in header files

			if (GetFileType(info->mText) == Src)
			{
				RAD_skipSymbols = false;
				return RAD_FindOrAddFileReference(wparam, lparam);
			}

			return 0;
		}
	}

	return RAD_FindOrAddFileReference(wparam, lparam);
}

// RAD Specific implementation!!!
LRESULT RenameReferencesDlg::OnSearchProgress(WPARAM prog, LPARAM lparam)
{
	if (!RAD_fieldPassSymScope.IsEmpty())
	{
		if (mRefs->RAD_pass == 1)
			prog /= 2u;
		else if (mRefs->RAD_pass == 2)
			prog = 50u + prog / 2u;
	}
	
	return __super::OnSearchProgress(prog, lparam);
}

LRESULT RenameReferencesDlg::RAD_FindOrAddFileReference(WPARAM wparam, LPARAM lparam)
{
	TreeReferenceInfo* info = reinterpret_cast<TreeReferenceInfo*>(wparam);

	UINT fileId = gFileIdManager->GetFileId(info->mText);

	auto found = RAD_fileTreeItems.find(fileId);
	if (found != RAD_fileTreeItems.cend())
	{
		info->mParentTreeItem = found->second;
		return (LRESULT)1;
	}

	LRESULT result = __super::OnAddFileReference(wparam, lparam);
	if (result)
	{
		RAD_fileTreeItems[fileId] = info->mParentTreeItem;
	}

	return result;
}

void RenameReferencesDlg::RAD_AddDesignerItem(const TreeReferenceInfo* info)
{
	if (RAD_designerItem)
		return;

	CStringW sym(mRefs->GetFindSym().Wide());
	CStringW wstr;
	//wstr.Format(L"%c%s%c Designer Updates", MARKER_BOLD, (LPCWSTR)sym, MARKER_NONE);
	wstr = L"Form Designer Updates";

	HTREEITEM it = m_tree.InsertItemW(wstr, ICONIDX_FILE, ICONIDX_FILE, info->mParentTreeItem);
	RAD_designerItem = it;
	mReferencesCount++;
	m_treeSubClass.SetItemFlags(it, TIF_PROCESS_MARKERS);

	m_tree.SetItemImage(it, ICONIDX_FILE, ICONIDX_FILE);
	m_tree.SetCheck(it, TRUE);
	m_tree.SetItemData(it, (uint)((kRADDesignerRefBit | info->mRefId) & ~kFileRefBit));
	if (m_tree.GetSelectedItem())
		mLastRefAdded = NULL;
	else
		mLastRefAdded = it;

#ifndef ALLOW_MULTIPLE_COLUMNS
	InterlockedIncrement(&m_treeSubClass.reposition_controls_pending);
#endif
}

void RAD_DesignerReferences::RAD_FixReplaceString(int pass, const WTString& findText, WTString& replaceText)
{
	if (!RAD_DesignerRefsInfo)
		return;

	if (!findText.IsEmpty() && !replaceText.IsEmpty())
	{
		if (pass == 1 && findText[0] == 'T' && replaceText[0] != 'T')
		{
			replaceText.insert(0, "T");
		}
		else if (pass == 2 && findText[0] != 'T' && replaceText[0] == 'T')
		{
			replaceText = replaceText.substr(1);
		}
	}
}
#endif

BEGIN_MESSAGE_MAP(RenameReferencesDlg, UpdateReferencesDlg)
ON_BN_CLICKED(IDC_CHK_CORE_REDIRECTS, OnToggleCoreRedirects)
END_MESSAGE_MAP()


