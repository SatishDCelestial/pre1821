#include "stdafxed.h"
#include "RenameFilesDlg.h"
#include "PROJECT.H"
#include "FILE.H"
#include "fdictionary.h"
#include "wt_stdlib.h"
#include "Edcnt.h"
#include "VaMessages.h"
#include "VACompletionBox.h"
#include "UndoContext.h"
#include "FreezeDisplay.h"
#include "StatusBarAnimator.h"
#include "DevShellAttributes.h"
#include "FindReferencesThread.h"
#include "VARefactor.h"
#include "Settings.h"
#include "VAAutomation.h"
#include "ProjectInfo.h"
#include "Expansion.h"
#include "StringUtils.h"
#include "WindowUtils.h"
#include "includesDb.h"
#include "RegKeys.h"
#include "VAWatermarks.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

RenameFilesDlg::RenameFilesDlg(CStringW currentFileName, DType* sym)
    : ReferencesWndBase("RenameFileDlg", IDD, NULL, false), mAutoRename(false), mDoRename(false),
      mCanceledManualRename(false)
{
	SetHelpTopic("dlgRenameFiles");

	m_originalFileName = currentFileName;
	m_originalName = GetBaseNameNoExt(currentFileName);
	if (sym && !sym->IsEmpty())
		m_newName = sym->Sym().Wide();
	else
		m_newName = m_originalName;

	AddFileToIncludesMap(m_originalFileName);

	FileList possibleMatches;
	GetRelatedProjectSourceFiles(m_originalFileName, possibleMatches, false);

	if (Psettings->mUnrealEngineCppSupport && possibleMatches.size() && m_originalFileName.Find(L".generated.") == -1)
	{
		// [case: 115255] update file.generated.h include directives
		CStringW slnDir = Path(GlobalProject->SolutionFile());
		ProjectVec projectVec = GlobalProject->GetProjectForFile(m_originalFileName);
		CStringW genFileName = Basename(m_originalFileName);
		int lastDot = genFileName.ReverseFind(L'.');

		// [case: 164542] in UE5 the location of .generated.h files has changed so we need to get correct location
		CStringW genIncLoc = L"\\intermediate\\build\\win64\\ue4editor\\inc\\"; // default UE4 .generated.h include location
		if (GlobalProject)
		{
			std::pair<int, int> ueVersionNumber = GlobalProject->GetUnrealEngineVersionAsNumber();
			if (ueVersionNumber.first >= 5)
				genIncLoc = L"\\intermediate\\build\\win64\\unrealeditor\\inc\\";
		}
		
		if (lastDot != -1)
		{
			genFileName = genFileName.Mid(0, lastDot);
			genFileName.Append(L".generated.h");

			for (const ProjectInfoPtr& projInfoPtr : projectVec)
			{
				CStringW projName = Basename(projInfoPtr->GetProjectFile());
				lastDot = projName.ReverseFind(L'.');

				if (lastDot != -1)
				{
					const FileList& incDirs = projInfoPtr->GetIncludeDirs();
					std::vector<CStringW> intDirs;

					for (const FileInfo& fileInfo : incDirs)
					{
						if (fileInfo.mFilenameLower.Find(genIncLoc) != -1)
							if (fileInfo.mFilenameLower.Find(L"engine") == -1)
								if (fileInfo.mFilenameLower.Find(L"plugins") == -1)
									intDirs.push_back(fileInfo.mFilename);
					}

					CStringW genFilePath;

					for (const CStringW& intDir : intDirs)
					{
						CStringW testPath = intDir + L'\\' + genFileName;

						if (IsFile(testPath))
						{
							genFilePath = testPath;
							break;
						}
					}

					if (genFilePath.GetLength() > 0)
					{
						possibleMatches.Add(genFilePath);
						break;
					}
				}
			}
		}
	}

	for (auto matchIter = possibleMatches.begin(); matchIter != possibleMatches.end(); ++matchIter)
		AddFileToIncludesMap(matchIter->mFilename);

	EdCntPtr ed(g_currentEdCnt);
	if (ed && Src == ed->m_ftype)
	{
		// [case: 81547]
		// if cur file is cpp, check for h
		bool hasHeader = false;
		for (auto iter = mMap.begin(); iter != mMap.end(); ++iter)
		{
			auto& eFilePath = iter->first;
			int ftype = ::GetFileType(eFilePath.mOldName, false, false);
			if (Header == ftype)
			{
				hasHeader = true;
				break;
			}
		}

		if (!hasHeader)
		{
			CStringW altFile(m_originalFileName);
			if (::SwapExtension(altFile))
			{
				int ftype = ::GetFileType(altFile, false, false);
				if (Header == ftype)
				{
					// match found
					AddFileToIncludesMap(altFile);
				}
			}
		}
	}

	mFirstVisibleLine = ed ? ed->GetFirstVisibleLine() : 0;
}

RenameFilesDlg::~RenameFilesDlg()
{
	if (mDoRename)
		DoRename();
}

BEGIN_MESSAGE_MAP(RenameFilesDlg, ReferencesWndBase)
//{{AFX_MSG_MAP(FindUsageDlg)
ON_BN_CLICKED(IDC_RENAME, OnRename)
ON_EN_CHANGE(IDC_EDIT1, OnChangeNameEdit)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void RenameFilesDlg::OnChangeNameEdit()
{
	CheckName();
}

void RenameFilesDlg::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);
}

void RenameFilesDlg::AddFileToIncludesMap(CStringW fileName)
{
	if (!IsFile(fileName))
		return;

	DTypeList includedBys;
	IncludesDb::GetIncludedBys(fileName, DTypeDbScope::dbSolution, includedBys);

	CheckedDTypeList eList;
	for (auto iter = includedBys.begin(); iter != includedBys.end(); ++iter)
	{
		// watch out for stale info
		if (IsFile(iter->FilePath()))
			eList.push_back(*iter);
	}
	mMap.push_back(MapEntry(CheckedFileName(fileName), eList));
}

BOOL RenameFilesDlg::OnInitDialog()
{
	ReferencesWndBase::OnInitDialog(false);
	m_tree.ModifyStyle(0, TVS_CHECKBOXES);

	mEdit_subclassed.SubclassDlgItem(IDC_EDIT1, this);
	mEdit_subclassed.SetText(m_newName);
	mEdit = &mEdit_subclassed;
	::mySetProp(mEdit->GetSafeHwnd(), "__VA_do_not_colour", (HANDLE)1);
	AddSzControl(IDC_EDIT1, mdResize, mdNone);
	AddSzControl(IDC_RENAME, mdRepos, mdNone);

	::VsScrollbarTheme(m_hWnd);
	if (CVS2010Colours::IsExtendedThemeActive())
	{
		Theme.AddDlgItemForDefaultTheming(IDC_STATUS);
		Theme.AddDlgItemForDefaultTheming(IDC_PROGRESS1);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDC_RENAME, this);
		Theme.AddThemedSubclasserForDlgItem<CThemedButton>(IDCANCEL, this);
		ThemeUtils::ApplyThemeInWindows(TRUE, m_hWnd);
	}

	_ASSERTE(mEdit);

	CheckName();
	PopulateTree();
	// removed & from text because the ansi -> unicode EDIT substitution breaks
	// dialog mnemonic ordering for the static control
	((CStatic*)GetDlgItem(IDC_STATUS))->SetWindowText("Rename files to:");

	mEdit->SetSel(0, 1000);
	mEdit->SetFocus();

	VAUpdateWindowTitle(VAWindowType::RenameFiles, *this);

	// we set focus to the edit ctrl
	return FALSE;
}

void RenameFilesDlg::FindCurrentSymbol(const WTString& symScope, int typeImgIdx)
{
	_ASSERTE(0);
}

void RenameFilesDlg::OnRename()
{
	if (mEdit)
		mEdit_subclassed.GetText(m_newName);

	// sync mMap to m_tree
	UpdateMap(m_tree.GetRootItem());

	mRenameMap.clear();

	int errorCount = BuildRenameMap(mRenameMap);
	if (errorCount)
		return; // Error message has already been displayed. Don't close dlg

	OnCancel();

	// Rename handled in dtor once out of dlg modal message loop
	mDoRename = true;
}

void RenameFilesDlg::DoRename()
{
	_ASSERTE(mDoRename);
	CWaitCursor cur;

	::RedrawWindow(MainWndH, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
	//	StatusBarGeneralAnimation anim(mAutoRename);
	UndoContext undoContext("VA Rename Files");

	RefactoringActive::SetCurrentRefactoring(
	    VARef_RenameFilesFromMenuCmd); // not exactly correct -- could be VARef_RenameFilesFromEditor

	RenameMap rm1, rm2;
	for (const auto& p : mRenameMap)
	{
		if (!DoesFilenameCaseDiffer(p.first, p.second))
		{
			rm1.insert(p); // if proper rename will be done, just do as before
			continue;
		}

		// generate temporary filename for two-steps rename
		GUID guid;
		::CoCreateGuid(&guid);
		wchar_t guid_string[64] = {0};
		::StringFromGUID2(guid, guid_string, _countof(guid_string));
		guid_string[0] = L'_'; // strip {}
		guid_string[wcslen(guid_string) - 1] = 0;

		std::wstring temp = (const wchar_t*)p.second;
		int i;
		for (i = (int)temp.size() - 1; i >= 0; i--)
		{
			switch (temp[(uint)i])
			{
			case L'\\':
			case L'/':
				i = -1; // insert at the end
				goto out;
			case L'.': // insert before the extension
				goto out;
			}
		}
	out:
		temp.insert((i < 0) ? temp.end() : (temp.begin() + i), &guid_string[0], &guid_string[wcslen(guid_string)]);

		rm1[p.first] = temp.c_str();
		rm2[temp.c_str()] = p.second;
	}

	int errorCount = RenameDocuments(rm1);
	if (rm2.size() > 0)
		errorCount += RenameDocuments(rm2);
	if (!mCanceledManualRename)
	{
		FreezeDisplay _f(FALSE);
		errorCount += UpdateIncludes();
	}

	RefactoringActive::SetCurrentRefactoring(0);

	if (errorCount)
	{
		OnRefactorErrorMsgBox();
	}

	// 	if(orgFile.GetLength())
	// 	{
	// 		DelayFileOpen(orgFile);
	// 		if (g_currentEdCnt)
	// 		{
	// 			if (gShellAttr->IsDevenv())
	// 			{
	// 				// [case: 46551] restore first visible line
	// 				ulong topPos = g_currentEdCnt->LinePos(mFirstVisibleLine);
	// 				if (-1 != topPos)
	// 				{
	// 					g_currentEdCnt->SetSel(topPos, topPos);
	// 					SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (long)_T("Edit.ScrollLineTop"), 0);
	// 					// don't use gDTE->ExecuteCommand since our dlg proc is running(?)
	// 					// works in vs2010 but not earlier IDEs
	// 				}
	// 			}
	//
	// 			g_currentEdCnt->SetPos(orgPos);
	// 		}
	// 	}
	//
}

void RenameFilesDlg::OnSearchComplete(int fileCount, bool wasCanceled)
{
	_ASSERTE(0);
}

void RenameFilesDlg::UpdateStatus(BOOL done, int fileCount)
{
	_ASSERTE(0);
}

void RenameFilesDlg::OnCancel()
{
	HWND h = mEdit_subclassed.UnsubclassWindow();
	::DestroyWindow(h);
	__super::OnCancel();
}

void RenameFilesDlg::OnDoubleClickTree()
{
	__super::OnDoubleClickTree();
	SetFocus();
}

void RenameFilesDlg::PopulateTree()
{
	auto filesNode = m_tree.InsertItem("Files to rename", ICONIDX_FILE_FOLDER, ICONIDX_FILE_FOLDER);
	m_tree.SetCheck(filesNode);
	m_treeSubClass.SetItemFlags(filesNode, TIF_PROCESS_MARKERS | TIF_ONE_CELL_ROW | TIF_DRAW_IN_BOLD | TIF_DONT_COLOUR |
	                                           TIF_DONT_COLOUR_TOOLTIP);

	HTREEITEM includesNode = NULL;

	for (auto iter = mMap.begin(); iter != mMap.end(); ++iter)
	{
		auto& eFilePath = iter->first;
		int imgIdx = GetFileImgIdx(eFilePath.mOldName);

		auto fileNode = m_tree.InsertItemW(eFilePath.mOldName, imgIdx, imgIdx, filesNode);
		m_tree.SetCheck(fileNode);
		m_tree.SetItemData(fileNode, (DWORD_PTR)&eFilePath.mChecked);
		m_treeSubClass.SetItemFlags(fileNode, TIF_PROCESS_MARKERS | TIF_ONE_CELL_ROW | TIF_DONT_COLOUR |
		                                          TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);

		auto& includes = iter->second;
		for (auto iter2 = includes.begin(); iter2 != includes.end(); ++iter2)
		{
			if (!includesNode)
			{
				includesNode =
				    m_tree.InsertItem("Files with #includes to update", ICONIDX_FILE_FOLDER, ICONIDX_FILE_FOLDER);
				m_tree.SetCheck(includesNode);
				m_treeSubClass.SetItemFlags(includesNode, TIF_PROCESS_MARKERS | TIF_ONE_CELL_ROW | TIF_DRAW_IN_BOLD |
				                                              TIF_DONT_COLOUR | TIF_DONT_COLOUR_TOOLTIP);
			}

			auto& checkedDType = *iter2;
			DType& sym = checkedDType.mSym;
			CStringW incTmp = sym.FilePath();
			int imgIdx2 = GetFileImgIdx(incTmp);
			auto includeNode = m_tree.InsertItemW(incTmp, imgIdx2, imgIdx2, includesNode);
			m_tree.SetCheck(includeNode);
			m_tree.SetItemData(includeNode, (DWORD_PTR)(&checkedDType.mChecked));
			m_treeSubClass.SetItemFlags(includeNode, TIF_PROCESS_MARKERS | TIF_ONE_CELL_ROW | TIF_DONT_COLOUR |
			                                             TIF_DONT_COLOUR_TOOLTIP | TIF_PATH_ELLIPSIS);
		}
	}

	m_tree.Expand(filesNode, TVE_EXPAND);
	if (includesNode)
	{
		m_tree.SortChildren(includesNode);
		m_tree.Expand(includesNode, TVE_EXPAND);
	}
	m_tree.SelectItem(filesNode);

	if (gTestLogger && gTestLogger->IsDialogLoggingEnabled())
	{
		WTString txt("RenameFiles: initial name = " + m_newName);
		gTestLogger->LogStr(txt);
		LogTree(m_tree.GetRootItem());
	}
}

void RenameFilesDlg::LogTree(HTREEITEM item)
{
	while (item)
	{
		WTString txt = (const char*)m_tree.GetItemText(item);
		txt = Basename(txt);
		txt = WTString("RenameFiles: ") + txt;
		gTestLogger->LogStr(txt);

		HTREEITEM childItem = m_tree.GetChildItem(item);
		LogTree(childItem);

		item = m_tree.GetNextSiblingItem(item);
	}
}

void RenameFilesDlg::GoToItem(HTREEITEM item)
{
	// 	DType *sym = (DType *)m_tree.GetItemData(item);
	// 	if (sym)
	// 	{
	// 	}
}

void RenameFilesDlg::UpdateMap(HTREEITEM item)
{
	while (item)
	{
		DWORD_PTR ptr = m_tree.GetItemData(item);
		if (ptr)
		{
			bool* checked = (bool*)ptr;
			*checked = !!m_tree.GetCheck(item);
		}

		HTREEITEM childItem = m_tree.GetChildItem(item);
		UpdateMap(childItem);

		item = m_tree.GetNextSiblingItem(item);
	}
}

int RenameFilesDlg::UpdateIncludes()
{
	int errorCount = 0;
	bool renameLimitMode = false;
	DWORD renameLimit = Psettings->mRestrictFilesOpenedDuringRefactor;
	for (auto iter = mMap.begin(); iter != mMap.end(); ++iter)
	{
		auto& checkedFileName = iter->first;
		// don't update #includes unless new file actually exists.
		const CStringW kFilenameToCheck(gTestsActive ? checkedFileName.mOldName : checkedFileName.mNewName);
		if (IsFile(kFilenameToCheck))
		{
			auto& includedBys = iter->second;

			if (includedBys.size() > renameLimit)
			{
				// raise message box notifying that there are more than 50 files and that rename will not be in one run
				WTString message = "Due to the large number of files with #include directives to be updated, changes "
				                   "to the files will have to be saved and editors "
				                   "closed during the operation causing loss of the ability to use undo. The operation "
				                   "will be performed one file at a time and may "
				                   "take a considerable amount of time to complete.\n\nDo you want to continue?";

				if (IDNO == WtMessageBox(message.Wide(), L"Edit Files?", MB_YESNO | MB_ICONWARNING))
					return errorCount;
				else
					renameLimitMode = true;
			}

			for (auto includedByIter = includedBys.begin(); includedByIter != includedBys.end(); ++includedByIter)
			{
				auto& checkedDType = *includedByIter;
				bool isInclChecked = checkedDType.mChecked;
				if (isInclChecked)
				{
					auto& sym = checkedDType.mSym;
					CStringW fileWithInclude = sym.FilePath();

					if (!gTestsActive)
					{
						// Check if fileWithInclude has been renamed!!!
						for (auto newIter = mMap.begin(); newIter != mMap.end(); ++newIter)
						{
							if (!fileWithInclude.CompareNoCase(newIter->first.mOldName))
							{
								fileWithInclude = newIter->first.mNewName;
								break;
							}
						}
					}

					int line = sym.Line();
					EdCntPtr ed = DelayFileOpen(fileWithInclude, line);
					if (ed)
					{
						long p1 = (long)ed->LinePos(line);
						long p2 = (long)ed->LinePos(line + 1);
						ed->SetSel(p1, p2);
						const CStringW kOrigLineStr(ed->GetSelStringW());
						CStringW lineStr(kOrigLineStr);

						// update include file name, but watch out for
						// #include "../foo/foo.h" <- don't change dir name
						{
							int pos = lineStr.FindOneOf(L"\"<");
							if (pos < 0)
								continue;
							pos++;

							CStringW tmp = lineStr.Mid(pos);
							int slash = 0;
							while ((slash = tmp.FindOneOf(L"\\/")) >= 0)
							{
								slash++;
								tmp = tmp.Mid(slash);
								pos += slash;
							}
							CStringW left = lineStr.Left(pos);
							CStringW right = lineStr.Mid(pos);

							if (FindNoCase(right, m_originalName + L".") == 0)
								ReplaceNoCase(right, m_originalName, m_newName);
							lineStr = left + right;
						}

						if (gTestsActive)
						{
							if (gTestLogger)
							{
								WTString msg;
								msg.WTFormat("RenameFile: update include in '%s' from [%s] to [%s]",
								             (LPCTSTR)CString(::Basename(fileWithInclude)),
								             (LPCTSTR)CString(kOrigLineStr), (LPCTSTR)CString(lineStr));
								gTestLogger->LogStr(msg);
							}
						}
						else
						{
							if (ed->ReplaceSelW(lineStr, noFormat))
							{
								ed->SendMessage(WM_COMMAND, WM_VA_REPARSEFILE);
								ed->OnModified(TRUE);

								if (renameLimitMode)
								{
									if (gTestsActive && gTestLogger)
									{
										gTestLogger->LogStr(WTString("Skipping Save and Close during AST."));
									}
									else
									{
										::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND,
										              (WPARAM)(LPCSTR) "File.SaveSelectedItems", 0);
										::SendMessage(gVaMainWnd->GetSafeHwnd(), VAM_EXECUTECOMMAND, 
													  (WPARAM)(LPCSTR) "File.Close", 0);
									}
								}
							}
							else
							{
								++errorCount;
							}
						}
					}
					else
					{
						++errorCount;
					}
				}
			}
		}
		else
		{
			++errorCount;
		}
	}
	return errorCount;
}

int RenameFilesDlg::RenameDocuments(const RenameMap& rm)
{
	if (rm.size() == 0)
		return 0;

	// [case: 80236]
	VsProjectList vsProjects;
	BuildSolutionProjectList(vsProjects);

	CStringW firstFile = rm.begin()->first;

	if (Psettings->mUnrealEngineCppSupport && rm.size() > 1 && firstFile.Find(L".generated.") != -1)
	{
		// [case: 115255] try using a different file to determine the owning project, because Unreal generated files
		// are always outside of the project
		auto it = rm.begin();
		++it;
		firstFile = it->first;
	}

	ProjectVec projectVec = GlobalProject->GetProjectForFile(firstFile);

	int errorCount = 0;
	// iterate over our project list (ordered so that shared projects are first)
	for (auto iter = projectVec.begin(); iter != projectVec.end(); ++iter)
	{
		const ProjectInfoPtr p = *iter;
		const CStringW iterFilePath = p->GetProjectFile();

		// iterate over solution projects
		for (auto vsPrj = vsProjects.begin(); vsPrj != vsProjects.end(); ++vsPrj)
		{
			auto& pProject = *vsPrj;
			CComBSTR projFilePathBstr;
#ifdef AVR_STUDIO
			pProject->get_FullName(&projFilePathBstr);
#else
			pProject->get_FileName(&projFilePathBstr);
#endif
			const CStringW projFilePath(projFilePathBstr);

			// don't walk all projects, only those that contain
			// the files we're renaming (ie, projects in the ProjectVector)
			if (!projFilePath.CompareNoCase(iterFilePath))
			{
				errorCount += WalkProject(pProject, rm);
				const CStringW ext(::GetBaseNameExt(projFilePath));
				if (!ext.CompareNoCase(L"shproj"))
				{
					// [case: 80931] C# .shproj don't send AddFile/RemoveFile (vcxitems works due to vcengine events)
					for (auto fileIt = rm.begin(); fileIt != rm.end(); ++fileIt)
					{
						if (p->ContainsFile(fileIt->first) && ::IsFile(fileIt->second))
						{
							GlobalProject->RemoveFile(projFilePath, fileIt->first);
							GlobalProject->AddFile(projFilePath, fileIt->second);
						}
					}
				}
			}
		}

		if (vsProjects.empty() && p->IsPseudoProject() && iterFilePath != Vsproject::kExternalFilesProjectName)
		{
			// [case: 102080] support for folder-based solutions
			// [case: 102486] cmake
			const CStringW projFile(::Basename(iterFilePath));
			if (!projFile.CompareNoCase(L"cppproperties.json") || !projFile.CompareNoCase(L"cmakelists.txt") ||
			    ::IsDir(iterFilePath))
			{
				for (auto fileIt = rm.begin(); fileIt != rm.end(); ++fileIt)
				{
					const CStringW fullName(fileIt->first);
					if (::IsFile(fullName) && p->ContainsFile(fullName))
					{
						const CStringW newName(fileIt->second);
						const CStringW baseName(::Basename(newName));
						vLog("RenameFile: manual rename of folder-based sln file %s to %s",
						     (LPCTSTR)CString(::Basename(fullName)), (LPCTSTR)CString(baseName));

						::DelayFileOpen(fullName);
						::SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("File.Close"), 0);
						GlobalProject->RemoveFile(iterFilePath, fullName);
						::DuplicateFile(fullName, ::AutoSaveFile(fullName));
						::_wrename(fullName, newName);

						if (!::IsFile(newName))
						{
							// wait and check again
							::Sleep(500);
							if (!::IsFile(newName))
								++errorCount;
						}

						GlobalProject->AddFile(iterFilePath, newName);
						::DelayFileOpen(newName);
					}
				}
			}
		}
	}

	if (!errorCount)
	{
		// [case: 81531]
		bool prompted = false;
		for (auto iter = rm.begin(); iter != rm.end(); ++iter)
		{
			const CStringW fullName(iter->first);
			const CStringW newName(iter->second);
			const CStringW baseName(::Basename(newName));
			if (::IsFile(newName) || !::IsFile(fullName))
				continue;

			// [case: 115255] unreal engine *.generated.* files live outside the project, but do not prompt for rename
			bool isUnrealGenerated = Psettings->mUnrealEngineCppSupport && fullName.Find(L".generated.") != -1;

			if (gTestsActive)
			{
				if (gTestLogger && (!vsProjects.size() || isUnrealGenerated))
				{
					WTString msg;
					msg.WTFormat("RenameFile (manual) from %s to %s", (LPCTSTR)CString(::Basename(iter->first)),
					             (LPCTSTR)CString(baseName));
					gTestLogger->LogStr(msg);
				}
			}
			else
			{
				// failed to identify a project so do manual rename on disk
				if (!prompted && !isUnrealGenerated)
				{
					WTString msg;
					msg.WTFormat("No project association was identified for %s.\n\nDo you want to continue the rename "
					             "operation anyway?",
					             (LPCTSTR)CString(fullName));
					if (!prompted && IDNO == ::WtMessageBox(msg, IDS_APPNAME, MB_YESNO))
					{
						mCanceledManualRename = true;
						break;
					}

					prompted = true;
				}

				vLog("RenameFile: manual rename of %s to %s", (LPCTSTR)CString(::Basename(iter->first)),
				     (LPCTSTR)CString(baseName));
				::DelayFileOpen(fullName);
				::SendVamMessageToCurEd(VAM_EXECUTECOMMAND, (WPARAM) _T("File.Close"), 0);
				GlobalProject->RemoveFile(CStringW(), fullName);
				::DuplicateFile(fullName, ::AutoSaveFile(fullName));
				::_wrename(fullName, newName);

				if (!::IsFile(newName))
				{
					// wait and check again
					::Sleep(500);
					if (!::IsFile(newName))
						++errorCount;
				}

				GlobalProject->AddFile(CStringW(), newName);
				::DelayFileOpen(newName);
			}
		}
	}

	if (gTestsActive)
	{
		if (gTestLogger)
			gTestLogger->LogStr(WTString("RenameFiles does not check IsFile"));
	}
	else if (!mCanceledManualRename)
	{
		for (auto iter = rm.begin(); iter != rm.end(); ++iter)
		{
			if (!IsFile(iter->second))
				++errorCount;
		}
	}

	return errorCount;
}

int RenameFilesDlg::WalkProject(CComPtr<EnvDTE::Project> pProject, const RenameMap& rm)
{
	int errorCount = 0;
	CComPtr<EnvDTE::ProjectItems> pItems;
	pProject->get_ProjectItems(&pItems);
	if (pItems)
	{
		CComBSTR projFilePathBstr;
#ifdef AVR_STUDIO
		pProject->get_FullName(&projFilePathBstr);
#else
		pProject->get_FileName(&projFilePathBstr);
#endif
		const CStringW projFilePath(projFilePathBstr);
		errorCount += WalkProjectItems(projFilePath, pItems, rm);
	}
	return errorCount;
}

// a reliable way to check if a file is open in the editor tabs
// g_EdCntList does not have the file in the list if it was never active since the project was opened
bool IsFileOpenInTabs(const CStringW& filePath)
{
	if (gDte == nullptr)
		return false;

	CComPtr<EnvDTE::Documents> pDocuments;
	HRESULT hr = gDte->get_Documents(&pDocuments);
	if (SUCCEEDED(hr) && pDocuments != nullptr)
	{
		long count = 0;
		hr = pDocuments->get_Count(&count);
		if (SUCCEEDED(hr))
		{
			// Iterate through open documents to check if filePath is open
			for (long i = 1; i <= count; i++) // 1-based index
			{
				CComPtr<EnvDTE::Document> pDocument;
				hr = pDocuments->Item(CComVariant(i), &pDocument);
				if (SUCCEEDED(hr) && pDocument != nullptr)
				{
					CComBSTR bstrDocPath;
					pDocument->get_FullName(&bstrDocPath);
					CStringW docPath(bstrDocPath);
					if (filePath.CompareNoCase(docPath) == 0)
						return true; // File is open in an editor tab
				}
			}
		}
	}

	return false; // File is not open
}


int RenameFilesDlg::WalkProjectItems(const CStringW& projFilepath, CComPtr<EnvDTE::ProjectItems> pItems,
                                     const RenameMap& rm)
{
	int errorCount = 0;

	CComPtr<IUnknown> pUnk;
	if (SUCCEEDED(pItems->_NewEnum(&pUnk)) && pUnk != NULL)
	{
		CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum{pUnk};
		if (pNewEnum)
		{
			VARIANT varItem;
			VariantInit(&varItem);

			while (pNewEnum->Next(1, &varItem, NULL) == S_OK)
			{
				_ASSERTE(varItem.vt == VT_DISPATCH);
				CComQIPtr<EnvDTE::ProjectItem> pItem = varItem.pdispVal;
				VariantClear(&varItem);
				if (pItem)
				{
					short fileCount = 0;
					pItem->get_FileCount(&fileCount);
					if (fileCount > 0)
					{
						CComBSTR bstrFullname;
						pItem->get_FileNames(1, &bstrFullname);

						CStringW fullName = (const wchar_t*)bstrFullname;

						// [case: 91385] do case-insensitive string find in map
						CStringW newName;
						for (auto const& cur : rm)
						{
							if (!cur.first.CompareNoCase(fullName))
							{
								newName = cur.second;
								break;
							}
						}

						if (!newName.IsEmpty())
						{
							const CStringW baseName = Basename(newName);
							if (gTestsActive)
							{
								if (gTestLogger)
								{
									WTString msg;
									WTString projFile(::Basename(projFilepath));
									projFile.ReplaceAllRE(LR"((_|vs|\.)[0-9][0-9][0-9][0-9][.].+)", false,
									                      CStringW(L""));
									msg.WTFormat("RenameFile from %s to %s in project %s",
									             (LPCTSTR)CString(::Basename(fullName)), (LPCTSTR)CString(baseName),
									             projFile.c_str());
									gTestLogger->LogStr(msg);
								}
							}
							else
							{
								int reopenLine = -1;
								if (gShellAttr->IsDevenv16OrHigher() && ::IsFile(fullName))
								{
									const CStringW& openFileName = g_currentEdCnt->FileName();
									if (openFileName != fullName || // [case: 164436] fix for VS2019 breaks Add Class in VS2022 if applied to the active file.
										                            //                for non-active files: closing and reopening, breaks Add Class in VS2022.
										                            //                renaming without closing isn't reliable for them (does not always happen).
																	//                so we switch to the file if open and rename it while it's active which works
										                            //                reliably and doesn't break Add Class in VS2022. See the case for more information.
										gShellAttr->IsDevenv16())   // [case: 140846] keep the fix for VS2019 intact
									{
										if (gShellAttr->IsDevenv17()) // [case: 164436] switch to file in VS2022
										{
											if (IsFileOpenInTabs(fullName)) // [case: 164436] only need to switch if file is open, renaming works fine when the files isn't open
												DelayFileOpen(fullName);
										}
										else // close and reopen in VS2019 (logic is intact)
										{
											EdCntPtr pEd = GetOpenEditWnd(fullName);
											if (pEd)
											{
												// [case: 140846]
												// DTE is bad in vs2019 after rename via ProjectItem.
												// close and then reopen it
												reopenLine = pEd->CurLine();
												// close it before the actual rename, so that any changes can be saved properly
												pEd->SendVamMessage(VAM_CLOSE_DTE_DOC, (WPARAM)pEd->m_VSNetDoc, 0);
											}
										}
									}
								}

								// if file is in multiple projects, then won't exist after first
								// project has been processed
								bool dupeFile = false;
								if (IsFile(fullName))
								{
									DuplicateFile(fullName, AutoSaveFile(fullName));
								}
								else
								{
									// [case: 77850] [case: 77851]
									dupeFile = true;
									// restore original file name so that projectItem rename succeeds.
									// it will fail if original file no longer exists (renamed via a previous project)
									::_wrename(newName, fullName);
								}

								// do rename
								CComBSTR baseNameBstr = (const wchar_t*)baseName;
								HRESULT hr = pItem->put_Name(baseNameBstr);
								if (dupeFile && !IsFile(newName))
									::_wrename(fullName, newName);

								if (FAILED(hr))
								{
									vLog("ERROR: RenameFile: %s %08lx", (LPCTSTR)CString(baseName), hr);
									++errorCount;
								}
								else if (hr == S_FALSE)
								{
									// [case: 77850] [case: 77851]
									// hr can be S_FALSE, which could be a rename fail (?) but not an HRESULT error
									vLog("WARN: RenameFile: S_FALSE returned by VS rename of %s",
									     (LPCTSTR)CString(baseName));
									int ftype = GetFileType(newName);
									// [case: 101530]
									if (IsCFile(ftype))
										++errorCount;
								}

								if (!::IsFile(newName))
								{
									// wait and check again
									::Sleep(500);
									if (!::IsFile(newName))
										++errorCount;
								}

								if (-1 != reopenLine &&
									!gShellAttr->IsDevenv17()) // [case: 164436] we don't close and reopen in VS2022
								{
									DelayFileOpen(newName, reopenLine);
								}
							}
						}
					}

					CComPtr<EnvDTE::ProjectItems> pSubItems;
					pItem->get_ProjectItems(&pSubItems);
					if (pSubItems)
						errorCount += WalkProjectItems(projFilepath, pSubItems, rm);
				}
			}
		}
	}

	return errorCount;
}

int RenameFilesDlg::SanityCheckMap(const RenameMap& m)
{
	int errorCount = 0;

	for (auto iter = m.begin(); iter != m.end(); ++iter)
	{
		auto& newFileName = iter->second;

		// check new file doesn't already exist
		if (IsFile(newFileName))
		{
			if (DoesFilenameCaseDiffer(iter->first, iter->second))
				continue; // allow if only filename case is changed

			CStringW msg;
			CString__FormatW(msg, L"The file \"%s\" already exists.\n", (LPCWSTR)newFileName);
			WtMessageBox(WTString(msg), IDS_APPNAME, MB_OK);
			return 1;
		}
		else
		{
			// check names are unique
			for (auto iter2 = m.begin(); iter2 != m.end(); ++iter2)
			{
				if (iter2 != iter && iter2->second == iter->second)
				{
					CStringW msg;
					CString__FormatW(msg, L"Can't rename multiple files to \"%s\".\n", (LPCWSTR)newFileName);
					WtMessageBox(WTString(msg), IDS_APPNAME, MB_OK);
					return 1;
				}
			}
		}
	}

	return errorCount;
}

int RenameFilesDlg::BuildRenameMap(RenameMap& rm)
{
	int errorCount = 0;
	int filesToRenameCnt = 0;
	for (auto iter = mMap.begin(); iter != mMap.end(); ++iter)
	{
		auto& checkedFileName = iter->first;
		bool isFileChecked = checkedFileName.mChecked;
		if (isFileChecked)
		{
			++filesToRenameCnt;

			CStringW fileName = checkedFileName.mOldName;
			//			auto& includedBys = iter->second;

			CStringW newFileName = BuildNewFilename(fileName, m_newName);

			rm[fileName] = newFileName;
			checkedFileName.mNewName = newFileName;
		}
	}

	if (!errorCount)
	{
		if (0 == filesToRenameCnt)
		{
			CString msg = "You have not selected any files to rename.  Continue?";
			if (IDNO == WtMessageBox(msg, IDS_APPNAME, MB_YESNO | MB_ICONQUESTION))
			{
				errorCount++;
			}
		}
	}

	if (!errorCount)
		errorCount += SanityCheckMap(rm);

	return errorCount;
}

void RenameFilesDlg::CheckName()
{
	CStringW newName;
	if (mEdit)
		mEdit_subclassed.GetText(newName);

	bool ok = false;

	if (newName.GetLength() > 0 && newName != m_originalName && -1 == newName.FindOneOf(L" \t\n\r") && // no white space
	    -1 == newName.FindOneOf(L"*?\"<>|") && // no invalid chars
	    -1 == newName.FindOneOf(L"\\/:") &&    // no path chars
	    -1 == newName.FindOneOf(L"."))         // no extensions
	{
		ok = true;
	}

	CButton* pRenameBtn = (CButton*)GetDlgItem(IDC_RENAME);
	if (pRenameBtn)
		pRenameBtn->EnableWindow(ok);
}

CStringW RenameFilesDlg::BuildNewFilename(CStringW fileName, CStringW newBasename)
{
	CStringW baseName = Basename(fileName);
	CStringW ext = "";
	int dotPos = baseName.Find('.');
	if (dotPos != -1)
		ext = baseName.Mid(dotPos);

	CStringW curPath = Path(fileName);
	curPath.Append(L"\\");
	CStringW newFileName = curPath + newBasename + ext;

	return newFileName;
}

void RenameFilesDlg::SetNewName(CStringW newName)
{
	if (!newName.IsEmpty())
		m_newName = newName;
}
