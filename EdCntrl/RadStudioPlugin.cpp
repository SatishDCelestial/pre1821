#include "stdafxed.h"

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
#include "edcnt.h"
#include "VaService.h"
#include "VaAddinClient.h"
#include "Settings.h"
#include "VaOptions.h"
#include "CppBuilder.h"
#include "RadStudioPlugin.h"
#include "RegKeys.h"
#include "VABrowseSym.h"
#include "EdDll.h"
#include "File.h"
#include "SemiColonDelimitedString.h"
#include "addin/DSCmds.h"
#include "VaPkg/VaPkgUI/PkgCmdID.h"
#include "ParseThrd.h"
#include "VaTimers.h"
#include "PerfTimer.h"
#include "VARefactor.h"
#include "FindReferences.h"
#include "VACompletionBox.h"
#include <xutility>
#include "SubClassWnd.h"
#include "SymbolTypes.h"
#include "VACompletionSet.h"
#include <ppl.h>
#include "serial_for.h"
#include "LiveOutlineFrame.h"
#include "HookCode.h"
#include "vaIPC/vaIPC/OnScopeExit.h"
#include "TraceWindowFrame.h"
#include "RadStudio/RadStudioFrame.h"
#include "RadStudio/RadStudioCompletion.h"
#include "RadStudio/RadStudioMenu.h"
#include "TempAssign.h"

#ifdef _DEBUG
#define TRACE_HOST_EVENTS
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

VaRadStudioPlugin::RsFrameworkType sActiveFramework = VaRadStudioPlugin::RsFrameworkType::None;

void EncodeEscapeChars(CStringW & wstr)
{
	LPCWSTR repl = nullptr;
	for (int i = wstr.GetLength() - 1; i >= 0; i--)
	{
		switch (wstr[i])
		{
			case L'\a': repl = L"\\a"; break;  // alert (bell)
			case L'\b': repl = L"\\b"; break;  // backspace
			case L'\f': repl = L"\\f"; break;  // form feed
			case L'\n': repl = L"\\n"; break;  // newline
			case L'\r': repl = L"\\r"; break;  // carriage return
			case L'\t': repl = L"\\t"; break;  // horizontal tab
			case L'\v': repl = L"\\v"; break;  // vertical tab
			case L'\\': repl = L"\\\\"; break; // backslash
			case L'\'': repl = L"\\'";  break; // single quote
			case L'\"': repl = L"\\\""; break; // double quote
		    default: repl = nullptr;
		}

		if (repl)
		{
			wstr.Delete(i);
			wstr.Insert(i, repl);
		}
	}
}

void AssertRadLC(int line, int charIndex)
{
#ifdef _DEBUG
	int startLine = -1, startCol = -1;
	HWND hWnd = nullptr;
	if (!gRadStudioHost->GetFocusedEditorCursorPos(nullptr, 0, &startLine, &startCol, &hWnd))
	{
		return;
	}

	if (line != startLine || startCol != charIndex)
	{
		ASSERT(!"Line, CharIndex MISMATCH!");
	}
#endif
}

VaRadStudioPlugin* gVaRadStudioPlugin = nullptr;

// RadStudio uses a single edit control in the main IDE window.
// Executing New Window creates a new top-level structure for independent edit controls.
// VA reuses EdCnt as the edit control switches between different text buffers.
// VA caches state here as different text buffers are activated.
// The FileOpenInEditor notification causes an entry to be created.
// The EditorFileChanged notification will cause an update to the stored buffer.
// The EditorViewActivated notification will cause g_currentEdCnt to check here.
// EditorViewActivated will likely cause g_currentEdCnt to update an entry here.
// And cause it to load from here.
struct RunningDocumentData
{
	// document state that is valid in all states
	UINT mFileId = 0;
	// the filename as used in host notifications, theoretically same as mNormalizedFilename
	CStringW mFilename;
	// the filename as stored in EdCnt, theoretically same as mFilename
	CStringW mNormalizedFilename;

	// valid in Connected and Disconnected states (once initially populated by host)
	WTString mBuffer;

  private:
	// saved editor state -- these members are valid in the Disconnected state;
	// but in the Connected state, the data is probably stale
	int m_bufState = 0;
	MultiParsePtr m_pmparse;
	LineMarkersPtr mMifMarkers;
	int mMifMarkersCookie = 0;
	int mMifSettingsCookie = 0;
	int m_modCookie = 0;
	WTString SymScope, SymDef;
	DType SymType;
	int m_ScopeLangType = 0;
// 	bool m_FileHasTimerForFullReparse = false;
// 	bool m_FileHasTimerForFullReparseASAP = false;
// 	bool m_FileIsQuedForFullReparse = false;
// 	uint m_lastScopePos = 0;
	WTString m_lastScope, mTagParserScopeType;
	int m_ftype = 0;
	WTString m_lastEditSymScope;
	bool mEolTypeChecked = false;
	EolTypes::EolType mEolType = EolTypes::eolCrLf;
// 	int m_LastPos1 = 0, m_LastPos2 = 0;
	bool mInitialParseCompleted = false;
	int m_preventGetBuf = 0;
	BOOL m_txtFile = FALSE;
	BOOL modified = false;
	bool modified_ever = false;

  private:
	// management state, valid in Initial state
	enum class DocState
	{
		Initial,     // received FileOpenInEditor notification from RS
		Connected,   // we have an active EdCnt so the data in here is probably stale
		Disconnected // data saved, released from EdCnt, but file still open in RS (no EditorClosed notification)
	};
	DocState mState = DocState::Initial;
	// this will either be empty, contain just the main editor, or a mix of main + new editor windows
	std::vector<EdCntPtr> mConnectedEds;
	bool mBufferModifiedWhileDisconnected = false;

  public:
	void Init(UINT fid, const wchar_t* filename, const char* AContent)
	{
		mBuffer = AContent;

		if (AContent == nullptr)
		{
			int length = gRadStudioHost->GetFileContentLength(filename);
			if (length)
			{
				std::vector<char> buff;
				buff.resize((size_t)length + 5); // ensure we have string \0 terminated
				if (gRadStudioHost->GetFileContent(filename, &buff.front(), length))
				{
					mBuffer = &buff.front();
				}
			}
		}

		mFileId = fid;
		mFilename = filename;
		mNormalizedFilename = ::NormalizeFilepath(filename);
		m_modCookie = (int)WTHashKeyW(filename);
	}

	void Connect(EdCntPtr ed)
	{
		_ASSERTE(ed);
		_ASSERTE(!IsConnected(ed));
		mConnectedEds.push_back(ed);

		PushStateToEditor(ed);

		if (mState == DocState::Initial)
		{
			_ASSERTE(mConnectedEds.size() == 1);
			mState = DocState::Connected;
			mBufferModifiedWhileDisconnected = false;
		}
		else if (mState == DocState::Disconnected)
		{
			_ASSERTE(mConnectedEds.size() == 1);
			mState = DocState::Connected;
			if (mBufferModifiedWhileDisconnected)
				mBufferModifiedWhileDisconnected = false;
		}
		else
		{
			// allow multiple editors to attach to same document
			// aside from mConnectedEds, this doc state is unchanged by additional views
			_ASSERTE(mConnectedEds.size() > 1);
		}
	}

	void Disconnect(EdCntPtr ed)
	{
		DisconnectWithoutStateUpdate(ed);
		if (!mConnectedEds.empty())
		{
			// other editors are still connected
			return;
		}

		UpdateStateFromEditor(ed);
		ed->RsEditorDisconnected();
	}

	void DisconnectWithoutStateUpdate(EdCntPtr ed)
	{
		_ASSERTE(ed);
		_ASSERTE(mState == DocState::Connected || mState == DocState::Initial);
		RemoveFromConnectedList(ed);

		if (!mConnectedEds.empty())
		{
			// other editors are still connected, so leave in connected state
			return;
		}

		mState = DocState::Disconnected;
		mBufferModifiedWhileDisconnected = false;
	}

	void UpdateContent(const char* AContent)
	{
		mBuffer = AContent;
		modified = TRUE;
		if (mState == RunningDocumentData::DocState::Disconnected)
		{
			mBufferModifiedWhileDisconnected = true;
			m_bufState = CTer::BUF_STATE_WRONG;
		}
		else
		{
			m_bufState = CTer::BUF_STATE_DIRTY;
		}

		for (const auto& ed : mConnectedEds)
		{
			if (ed->GetSafeHwnd())
			{
				ed->SetBufState(m_bufState);
				ed->OnModified(TRUE);			
			}
			else
			{
				ed->OnModified(FALSE);						
			}

			if (ed == g_currentEdCnt && gVaService->GetOutlineFrame())
			{
				gVaRadStudioPlugin->UpdateVaOutline();
			}
		}
	}

	bool IsConnected() const
	{
		_ASSERTE((mConnectedEds.empty() && mState != RunningDocumentData::DocState::Connected) || (!mConnectedEds.empty() && mState == RunningDocumentData::DocState::Connected));
		return mState == RunningDocumentData::DocState::Connected;
	}

	bool IsConnected(EdCntPtr ed) const
	{
		_ASSERTE((mConnectedEds.empty() && !IsConnected()) || (!mConnectedEds.empty() && IsConnected()));
		for (const auto& it : mConnectedEds)
		{
			if (it == ed)
			{
				// can't assert because we might be checking after state has changed;
				// just need to know of this ed is in this list of connected eds.
				// _ASSERTE(it->filename == ed->FileName());
				// _ASSERTE(it->filename == mNormalizedFilename);
				// _ASSERTE(ed->filename == mNormalizedFilename);
				return true;
			}
		}

		return false;
	}

	void TakeConnectedEditors(RunningDocumentData* prevDocData)
	{
		// set modified since this is likely a new file -- this method should
		// only be called in the weird case where activation notification fires
		// before file open notification fires, when a new file is created
		for (auto& it : prevDocData->mConnectedEds)
			Connect(it);

		prevDocData->mConnectedEds.clear();
	}

#pragma warning(disable: 4702)
	EdCntPtr GetAnyEd() const
	{
		if (!mConnectedEds.empty())
		{
			for (auto& it : mConnectedEds)
			{
				if (it.get())
					return it;					
			}
		}

		return nullptr;
	}
#pragma warning(default: 4702)

	bool BufferIsModified() const
	{
		return modified || mBufferModifiedWhileDisconnected;
	}

	bool BufferWasEverModified() const
	{
		return modified_ever;
	}

	static void ClearEdCnt(EdCntPtr ed)
	{
		ed->ClearData();
		ed->RsEditorConnected(true);
	}

	static void Release(EdCntPtr ed)
	{
		ed->ReleaseSelfReferences();
	}

  private:

	bool RemoveFromConnectedList(EdCntPtr ed)
	{
		_ASSERTE(IsConnected(ed));
		for (auto it = mConnectedEds.begin(); it != mConnectedEds.end(); ++it)
		{
			if (*it == ed)
			{
				mConnectedEds.erase(it);
				return true;
			}
		}

		return false;
	}

	void UpdateStateFromEditor(EdCntPtr ed)
	{
		_ASSERTE(ed);
		_ASSERTE(!mNormalizedFilename.CompareNoCase(ed->FileName()));
		_ASSERTE(mFileId == gFileIdManager->GetFileId(ed->FileName()));

		// state of last editor to disconnect gets saved -- helps ensure mBuffer 
		// is what is reported by host when there are multiple views for same file
		{
			AutoLockCs l(ed->mDataLock);
			mBuffer = ed->m_buf;
			SymScope = ed->SymScope;
			SymDef = ed->SymDef;
			SymType = ed->SymType;
		}

		m_bufState = ed->m_bufState;
		m_ScopeLangType = ed->m_ScopeLangType;
		m_ftype = ed->m_ftype;
		m_lastScope = ed->m_lastScope;
		mTagParserScopeType = ed->mTagParserScopeType;
		m_lastEditSymScope = ed->m_lastEditSymScope;
		m_pmparse = ed->GetNewestParseDb();
		mEolTypeChecked = ed->mEolTypeChecked;
		mEolType = ed->mEolType;
		mMifMarkers = ed->mMifMarkers;
		mMifMarkersCookie = ed->mMifMarkersCookie;
		mMifSettingsCookie = ed->mMifSettingsCookie;
		m_modCookie = ed->m_modCookie;
		mInitialParseCompleted = ed->mInitialParseCompleted;
		m_preventGetBuf = ed->m_preventGetBuf;
		m_txtFile = ed->m_txtFile;
		modified = ed->modified;
		modified_ever = ed->modified_ever;
	}

	void PushStateToEditor(EdCntPtr ed)
	{
		_ASSERTE(ed);
		{
			AutoLockCs l(ed->mDataLock);
			ed->filename = mNormalizedFilename;
			ed->m_buf = mBuffer;
			ed->mPreviousBuf = mBuffer;
			ed->SymScope = SymScope;
			ed->SymDef = SymDef;
			ed->SymType = SymType;
		}
		ed->m_bufState = m_bufState;
		ed->m_ScopeLangType = m_ScopeLangType;

		ed->m_ftype = m_ftype;
		ed->m_lastScope = m_lastScope;
		ed->mTagParserScopeType = mTagParserScopeType;
		ed->m_lastEditSymScope = m_lastEditSymScope;
		if (!m_pmparse)
		{
			m_pmparse = MultiParse::Create(Src);
			m_pmparse->SetCacheable(TRUE);
			m_pmparse->SetFilename(mNormalizedFilename);
		}
		else if (m_pmparse->GetFilename() != mNormalizedFilename)
			m_pmparse->SetFilename(mNormalizedFilename);
		ed->SetNewMparse(m_pmparse, FALSE);
		ed->mEolTypeChecked = mEolTypeChecked;
		ed->mEolType = mEolType;
		ed->mMifMarkers = mMifMarkers;
		ed->mMifMarkersCookie = mMifMarkersCookie;
		ed->mMifSettingsCookie = mMifSettingsCookie;
		ed->m_modCookie = m_modCookie;
		ed->mInitialParseCompleted = mInitialParseCompleted;
		ed->m_preventGetBuf = m_preventGetBuf;
		ed->m_txtFile = m_txtFile;
		ed->modified = modified;
		ed->modified_ever = modified_ever;

		ed->RsEditorConnected(mState == DocState::Initial);
	}
};

using RunningDocumentDataPtr = std::shared_ptr<RunningDocumentData>;
using RsRunningDocumentTable = std::map<UINT, RunningDocumentDataPtr>; // FileId -> RunningDocumentDataPtr
RsRunningDocumentTable gRsRunningDocuments; // protect access with VaRadStudioPlugin::mLock

static HWND LocateNewPopupEditor();

// #RAD_LineAndColumn
void LC_RAD_2_VA(int* line, int* column)
{
	if (column)
	{
		(*column) += 1;
	}
}

long LC_RAD_2_VA_POS(int line, int column)
{
	LC_RAD_2_VA(&line, &column);
	return TERRCTOLONG(line, column);
}

// #RAD_LineAndColumn
void LC_VA_2_RAD(int* line, int* column)
{
	if (line && *line == 0)
		*line = 1;

	if (column && *column)
	{
		(*column) -= 1;
	}
}

void __stdcall VaRadStudioPlugin::InitializeVA(NsRadStudio::TInitializeData initData)
{
#ifdef _DEBUG
	PerfTimer pt{"VARSP IntializeVA", true};
#endif
	InitializeMadCHook();

	mMainWnd = initData.FWindowHandle;
#if !defined(VA_CPPUNIT)
	_ASSERTE(mMainWnd);
	gVaAddinClient.SetMainWnd(mMainWnd);
#endif

	if (initData.FSystemInclude)
		EnvOptionsChanged(initData.FSystemInclude);

#if !defined(VA_CPPUNIT)
	_ASSERTE(initData.FRegistryKey);
	if (initData.FRegistryKey)
	{
		if (initData.FRegistryKey[0] == L'\\')
			mRegPath = &initData.FRegistryKey[1];
		else
			mRegPath = initData.FRegistryKey;

		_ASSERTE(mRegPath[mRegPath.GetLength() - 1] != L'\\');
	}

	_ASSERTE(initData.FAppDataRoamingPath);
	if (initData.FAppDataRoamingPath)
		mAppDataRoamingPath = initData.FAppDataRoamingPath;

	_ASSERTE(initData.FAppDataLocalPath);
	if (initData.FAppDataLocalPath)
		mAppDataLocalPath = initData.FAppDataLocalPath;
#endif

#if !defined(VA_CPPUNIT)
	sVaDllApp.DoInit();
#endif
	gVaAddinClient.LicenseInitComplete();
	IDEThemeChanged(); // before gVaService is created

	mInit = true;
	gVaAddinClient.SetupVa(nullptr);
	extern int g_screenXOffset;
	g_screenXOffset = 50;
#ifdef _DEBUG	
	Psettings->mMouseClickCmds = 0x00003321;
#else
	Psettings->mMouseClickCmds = 0;
#endif
	Psettings->TabSize = 1;

	VaRSMenuManager::InitMenus();

#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg,
	                 L"VARSP InitializeVA:\nHWND: %p class %s\nreg: %s\nroaming: %s\nlocal: %s\nincludes: %s\n",
	                 MainWndH, (LPCWSTR)::GetWindowClassString(MainWndH).Wide(), (LPCWSTR)mRegPath, (LPCWSTR)mAppDataRoamingPath, (LPCWSTR)mAppDataLocalPath, (LPCWSTR)mSysIncludeDirs);
	::OutputDebugStringW(msg);
#endif
}

void __stdcall VaRadStudioPlugin::Shutdown()
{
	// displaying a message box here causes an exception in BDS
#ifdef TRACE_HOST_EVENTS
	::OutputDebugStringW(L"VARSP Shutdown\n");
#endif

	VaRSFrameBase::OnShutdown();

	mMainEdCnt = nullptr;
	{
		AutoLockCs l(g_EdCntListLock);
		g_EdCntList.clear();
	}

	auto tmp = g_statBar;
	g_statBar = nullptr;
	delete tmp;

	if (gRadStudioHost)
		gRadStudioHost = nullptr;

	{
		AutoLockCs l(mLock);
		_ASSERTE(gRsRunningDocuments.empty());
		gRsRunningDocuments.clear();
	}

	if (mInit)
		gVaAddinClient.Shutdown();

#if !defined(VA_CPPUNIT)
	sVaDllApp.DoExit();
#endif

	_ASSERTE(gVaRadStudioPlugin);
	gVaRadStudioPlugin = nullptr;
	MainWndH = nullptr;

	FinalizeMadCHook();

	delete this;
}

void __stdcall VaRadStudioPlugin::ProjectGroupOpened(const wchar_t* AFileName, NsRadStudio::TProjectGroupData AProjectGroupData)
{
	if (!mInit)
		return;

#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP ProjectGroupOpened: %s\n", AFileName);
	::OutputDebugStringW(msg);
#endif

	mProjectsLoading = true;
	mProjectCount = 0;
	AutoLockCs l(mLock);
	mProjectGroupFile.Empty();
	mProjectGroup.clear();

	SaveRsProjectData(AFileName, &AProjectGroupData);
	NotifyProjectLoad();
}

void VaRadStudioPlugin::NotifyProjectLoad()
{
	CStringW projects;

	if (!mProjectGroupFile.IsEmpty() || !mProjectGroup.empty())
	{
		projects = mProjectGroupFile + L";";
		for (auto prj : mProjectGroup)
			projects += prj->mProject + L";";
	}

	gVaAddinClient.LoadWorkspace(projects);

	mProjectCount = (uint)mProjectGroup.size();
	mProjectsLoading = false;
}

void __stdcall VaRadStudioPlugin::ProjectGroupClosed()
{
	if (!mInit)
		return;

#ifdef TRACE_HOST_EVENTS
	::OutputDebugStringW(L"VARSP ProjectGroupClosed\n");
#endif

	{
		AutoLockCs l(mLock);
		mProjectGroupFile.Empty();
		mProjectGroup.clear();
		mProjectCount = 0;
	}

	gVaAddinClient.CloseWorkspace();

	AutoLockCs l(mLock);
	NotifyProjectLoad();
}

void __stdcall VaRadStudioPlugin::FindSymbolInProjectGroup()
{
	if (!mInit)
		return;

	::VABrowseSymDlg();
}

void __stdcall VaRadStudioPlugin::FindSymbolInProjectGroupUpdate(bool* LEnable, bool* LVisible)
{
	*LEnable = false;
	*LVisible = false;

	if (!mInit)
		return;

	if (gVaService)
	{
		const DWORD s = gVaService->QueryStatus(IVaService::ct_global, icmdVaCmd_FindSymbolDlg);
		*LVisible = s == 1 || s == 0;
		*LEnable = s == 1;
	}
}

void __stdcall VaRadStudioPlugin::IDEThemeChanged()
{
#ifdef TRACE_HOST_EVENTS
	if (mInit)
		::OutputDebugStringW(L"VARSP IDEThemeChanged\n");
#endif

	if (gRadStudioHost)
	{
#ifdef _DEBUG
		const auto prev1 = mCurTheme.FPadding1;
		const auto prev2 = mCurTheme.FPadding2;
#endif
		{
			AutoLockCs l(mLock);
			bool res = gRadStudioHost->GetTheming(&mCurTheme);
			_ASSERTE(res);
		}

		_ASSERTE(prev1 == mCurTheme.FPadding1);
		_ASSERTE(prev2 == mCurTheme.FPadding2);
		if (gVaService && mInit)
			gVaService->ThemeUpdated();
	}
}

void __stdcall VaRadStudioPlugin::EnvOptionsChanged(const wchar_t* ASystemInclude)
{
#ifdef TRACE_HOST_EVENTS
	if (mInit)
		::OutputDebugStringW(L"VARSP EnvOptionsChanged\n");
#endif

	AutoLockCs l(mLock);
	mSysIncludeDirs = ASystemInclude;
	if (mSysIncludeDirs[mSysIncludeDirs.GetLength() - 1] != L';')
		mSysIncludeDirs += L";";

	if (mSysIncludeDirs.FindOneOf(L"%$") != -1)
	{
#ifdef TRACE_HOST_EVENTS
		CStringW msg;
		CString__FormatW(msg, L"ERROR: EnvOptionsChanged has unresolved env variable in include dir: %s\n", (LPCWSTR)mSysIncludeDirs);
		::OutputDebugStringW(msg);
#endif
	}

	if (mInit)
	{
		// #cppbTODO VA should reload settings from registry if IDE uses own UI for VA settings
		gVaAddinClient.SettingsUpdated(VA_UpdateSetting_Reset);
		::VaOptionsUpdated();
	}
}

void __stdcall VaRadStudioPlugin::ProjectOptionsChanged(const wchar_t* AFileName, NsRadStudio::TFilesAndOptions* AFilesAndOptions)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP ProjectOptionsChanged: %s\n", AFileName);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	if (!AFilesAndOptions)
		return;

	RsProjectDataPtr updatedProj = ReadRsProject(AFilesAndOptions);
	_ASSERTE(updatedProj->mProject == AFileName);
	if (updatedProj->mProject != AFileName)
		return;

	mProjectsLoading = true;
	AutoLockCs l(mLock);
	// look up previous project, and replace it
	for (uint idx = 0; idx < mProjectGroup.size(); ++idx)
	{
		RsProjectDataPtr it = mProjectGroup[idx];
		if (it->mProject != AFileName)
			continue;

		RsProjectDataPtr oldProj = it;
		// we might be notified of a difference that we don't care about
		if (oldProj->HasDifference(*updatedProj))
		{
			mProjectGroup[idx] = updatedProj;

			// NotifyProjectLoad will cause reload; alternatively, figure out what changed and handle 
			// without reload if possible
			NotifyProjectLoad();
		}

		break;
	}
}

// this will get called for each file in a new project added to the project group
void __stdcall VaRadStudioPlugin::FileAdded(const wchar_t* AProjectName, const wchar_t* AFileName)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP FileAdded: %s\n", AFileName);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	{
		// update our project info, so that if we do a project load notify, our data is correct
		AutoLockCs l(mLock);
		mLastAddedFile = AFileName;
		for (uint idx = 0; idx < mProjectGroup.size(); ++idx)
		{
			RsProjectDataPtr it = mProjectGroup[idx];
			if (it->mProject != AProjectName)
				continue;

			const UINT fid = gFileIdManager ? gFileIdManager->GetFileId(AFileName) : 0;
			mFileRSFrameworkType[fid] = it->mFrameworkType;
			if (mActiveFileId == fid)
				sActiveFramework = it->mFrameworkType;

			if (it->ShouldIgnorePath(AFileName))
				continue;

			it->mSourceFiles.emplace_back(AFileName);
			break;
		}
	}

	class ProjectAddFileWorkItem : public ParseWorkItem
	{
	  public:
		ProjectAddFileWorkItem(const wchar_t* projectName, const wchar_t* fileName)
		    : ParseWorkItem("ProjectAddWorkItem"), mProjectName(projectName), mFileName(fileName)
		{
		}

		void DoParseWork() override
		{
			gVaAddinClient.AddFileToProject(mProjectName, mFileName, TRUE);
		}

		bool CanRunNow() const override
		{
			if (GlobalProject && GlobalProject->IsOkToIterateFiles())
				return true;
			return false;
		}

	  protected:
		CStringW mProjectName;
		CStringW mFileName;
	};

	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new ProjectAddFileWorkItem(AProjectName, AFileName));
}

void __stdcall VaRadStudioPlugin::FileRemoved(const wchar_t* AProjectName, const wchar_t* AFileName)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP FileRemoved: %s\n", AFileName);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	{
		const UINT fid = gFileIdManager ? gFileIdManager->GetFileId(AFileName) : 0;
		mFileRSFrameworkType.erase(fid);

		// update our project info, so that if we do a project load notify, our data is correct
		AutoLockCs l(mLock);
		for (uint idx = 0; idx < mProjectGroup.size(); ++idx)
		{
			RsProjectDataPtr it = mProjectGroup[idx];
			if (it->mProject != AProjectName)
				continue;

			std::vector<CStringW> srcFiles;
			srcFiles.reserve(it->mSourceFiles.size());
			for (const auto& srcFileIt : it->mSourceFiles)
			{
				if (srcFileIt == AFileName)
					continue;

				srcFiles.emplace_back(srcFileIt);
			}

			it->mSourceFiles = srcFiles;
			break;
		}
	}

	class ProjectRemoveFileWorkItem : public ParseWorkItem
	{
	  public:
		ProjectRemoveFileWorkItem(const wchar_t* projectName, const wchar_t* fileName)
		    : ParseWorkItem("ProjectRemoveFileWorkItem"), mProjectName(projectName), mFileName(fileName)
		{
		}

		void DoParseWork() override
		{
			gVaAddinClient.RemoveFileFromProject(mProjectName, mFileName, TRUE);
		}

		bool CanRunNow() const override
		{
			if (GlobalProject && GlobalProject->IsOkToIterateFiles())
				return true;
			return false;
		}

	  protected:
		CStringW mProjectName;
		CStringW mFileName;
	};

	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new ProjectRemoveFileWorkItem(AProjectName, AFileName));
}

void __stdcall VaRadStudioPlugin::FileRenamed(const wchar_t* AProjectName, const wchar_t* AOldFileName, const wchar_t* ANewFileName)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP FileRenamed: %s -> %s\n", AOldFileName, ANewFileName);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	CStringW newNormalizedFilename;

	{
		AutoLockCs l(mLock);

		// update our project info, so that if we do a project load notify, our data is correct
		for (RsProjectDataPtr it : mProjectGroup)
		{
			if (it->mProject != AProjectName)
				continue;

			for (auto& srcFileIt : it->mSourceFiles)
			{
				if (srcFileIt != AOldFileName)
					continue;

				srcFileIt = ANewFileName;
				break;
			}
			break;
		}

		RunningDocumentDataPtr pDocData;
		UINT fid = gFileIdManager ? gFileIdManager->GetFileId(AOldFileName) : 0;
		auto it = gRsRunningDocuments.find(fid);
		if (it != gRsRunningDocuments.end())
		{
			pDocData = it->second;
			// remove from map since the old fileId is the key, needs to be reinserted with new key
			gRsRunningDocuments.erase(it);
			if (pDocData)
			{
				const UINT newFid = gFileIdManager->GetFileId(ANewFileName);
				pDocData->mFileId = newFid;
				pDocData->mFilename = ANewFileName;
				newNormalizedFilename = pDocData->mNormalizedFilename = ::NormalizeFilepath(ANewFileName);
				gRsRunningDocuments[newFid] = pDocData;
			}
		}
	}

	class ProjectRenameFileWorkItem : public ParseWorkItem
	{
	  public:
		ProjectRenameFileWorkItem(const wchar_t* projectName, const wchar_t* oldFileName, 
								  const wchar_t* newFileName, const wchar_t* newNormalizedFilename)
		    : ParseWorkItem("ProjectRenameFileWorkItem"), mProjectName(projectName), 
			mOldFileName(oldFileName), mNewFileName(newFileName), mNewNormalizedFilename(newNormalizedFilename)
		{
		}

		void DoParseWork() override
		{
			gVaAddinClient.RenameFileInProject(mProjectName, mOldFileName, mNewFileName);
			if (gVaService)
			{
				gVaService->DocumentSavedAs(mNewFileName, mOldFileName);
				if (!mNewNormalizedFilename.IsEmpty())
					gVaService->DocumentModified(mNewNormalizedFilename, -1, -1, -1, -1, -1, -1, -1);
			}
		}

		bool CanRunNow() const override
		{
			if (GlobalProject && GlobalProject->IsOkToIterateFiles())
				return true;
			return false;
		}

	  protected:
		CStringW mProjectName;
		CStringW mOldFileName;
		CStringW mNewFileName;
		CStringW mNewNormalizedFilename;
	};

	if (g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new ProjectRenameFileWorkItem(AProjectName, AOldFileName, ANewFileName, newNormalizedFilename));
}

void __stdcall VaRadStudioPlugin::ProjectAddedToGroup(const wchar_t* AProjectName, NsRadStudio::TFilesAndOptions* AFilesAndOptions)
{
	if (!mInit)
		return;

	AutoLockCs l(mLock);
	if (mProjectGroupFile.IsEmpty())
	{
		// ignore if fired during project load before we get ProjectGroupOpened notification
#ifdef TRACE_HOST_EVENTS
		CStringW msg;
		CString__FormatW(msg, L"VARSP ProjectAddedToGroup (ignored): %s\n", AProjectName);
		::OutputDebugStringW(msg);
#endif
		return;
	}

#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP ProjectAddedToGroup: %s\n", AProjectName);
	::OutputDebugStringW(msg);
#endif

	mProjectsLoading = true;
	mProjectGroup.emplace_back(ReadRsProject(AFilesAndOptions));
	NotifyProjectLoad();
}

void __stdcall VaRadStudioPlugin::ProjectRemovedFromGroup(const wchar_t* AProjectName, NsRadStudio::TFilesAndOptions* AFilesAndOptions)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP ProjectRemovedFromGroup: %s\n", AProjectName);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	if (!AFilesAndOptions)
		return;

	RsProjectDataPtr updatedProj = ReadRsProject(AFilesAndOptions);
	_ASSERTE(updatedProj->mProject == AProjectName);
	if (updatedProj->mProject != AProjectName)
		return;

	// copy project group except for the project that was removed
	RsProjectGroup newGrp;
	AutoLockCs l(mLock);
	for (uint idx = 0; idx < mProjectGroup.size(); ++idx)
	{
		RsProjectDataPtr it = mProjectGroup[idx];
		if (it->mProject == AProjectName)
			continue;

		newGrp.push_back(it);
	}

	mProjectsLoading = true;
	mProjectGroup = newGrp;
	NotifyProjectLoad();
}

void __stdcall VaRadStudioPlugin::ProjectRenamed(const wchar_t* AProjectName, const wchar_t* AOldProjectName, const wchar_t* ANewProjectName)
{
	if (!mInit)
		return;

	const CStringW v1(AProjectName), v2(AOldProjectName), v3(ANewProjectName);
	if (v1 == v2 && v2 == v3)
	{
		CStringW msg;
		CString__FormatW(msg, L"VARSP ProjectRenamed (ignored, not actually renamed): %s\n", AProjectName);
		::OutputDebugStringW(msg);
		return;
	}

	_ASSERTE(v1 == v3);

#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP ProjectRenamed: %s %s\n", AOldProjectName, ANewProjectName);
	::OutputDebugStringW(msg);
#endif

	AutoLockCs l(mLock);
	for (RsProjectDataPtr it : mProjectGroup)
	{
		if (it->mProject != v2)
			continue;

		it->mProject = v3;
		break;
	}

	mProjectsLoading = true;
	NotifyProjectLoad();
}

// When loading a project group, this will fire before the ProjectGroupOpened notification,
// possibly for many files.
// It will also fire for units (unit.h, unit.cpp) even though only one half is visible.
// Can't ctrl+tab between .h/.cpp in a unit though both are open.
// No notification when user opens a new editor window (file is already open in a different editor).
void __stdcall VaRadStudioPlugin::FileOpenInEditor(const wchar_t* filename, const char* AContent) // AContent is a utf8char *)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP FileOpenInEditor: %s\n", filename);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	if (::ShouldIgnoreFile(filename, true))
	{
		AutoLockCs l(mLock);
		mLastOpenedFile.Empty();
		return;
	}

	{
		AutoLockCs l(mLock);
		mLastOpenedFile = filename;
	}

	UINT fid = gFileIdManager ? gFileIdManager->GetFileId(filename) : 0;
	if (fid)
	{
		AutoLockCs l(mLock);
		RunningDocumentDataPtr prevDocData;
		auto it = gRsRunningDocuments.find(fid);
		if (it != gRsRunningDocuments.end())
			prevDocData = it->second;

		RunningDocumentDataPtr p(std::make_shared<RunningDocumentData>());
		p->Init(fid, filename, AContent);

		if (prevDocData)
		{
			// we must have gotten an editor activation before the opened notification;
			// use the new docData, but take the connected editors from the old data
			p->TakeConnectedEditors(prevDocData.get());
		}

		gRsRunningDocuments[fid] = p;
	}
}

// host notification that the last editor of a file has been closed.
// note, some editors are not visible.
// not fired for all editors, only the last editor of a particular file.
void __stdcall VaRadStudioPlugin::EditorClosed(const wchar_t* filename)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP EditorClosed: %s\n", filename);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	{
		AutoLockCs l(mLock);
		if (mLastOpenedFile == filename)
			mLastOpenedFile.Empty();
	}

	const CStringW f(::NormalizeFilepath(filename));
	EdCntPtr ed = g_currentEdCnt;
	if (ed)
	{
		// don't clear EdCnt state -- we get no activation after close/reopen
	}

	if (ed != mMainEdCnt && g_currentEdCnt == ed)
	{
		if (!::IsWindow(ed->GetSafeHwnd()))
		{
			g_currentEdCnt = mMainEdCnt;
			CurrentEditorUpdated();
		}
	}

	UINT fid = gFileIdManager ? gFileIdManager->GetFileId(filename) : 0;
	if (fid)
	{
		AutoLockCs l(mLock);
		auto it = gRsRunningDocuments.find(fid);
		if (it != gRsRunningDocuments.end())
		{
			RunningDocumentDataPtr pDocData = it->second;
			if (pDocData && pDocData->IsConnected())
			{
				// we need to do some logic that EdCnt::ReleaseSelfReferences would normally run
				// when an editor is closed.  But in RadStudio, at this point, we don't really know
				// if the editor is closing -- just that the file is being closed.
				// Need to clear parsing of unsaved edits here instead of in EdCnt::ReleaseSelfReferences.
				EdCntPtr ed2 = pDocData->GetAnyEd();
				mLock.Unlock();

				// #EdCntReleaseSelfRefDupe
				if (pDocData->BufferIsModified() || pDocData->BufferWasEverModified() || 
					(ed2 && (ed2->Modified() || ed2->m_FileHasTimerForFullReparse || ed2->modified_ever)))
				{
					// force a reparse
					if (pDocData->BufferIsModified() || StopIt || (ed2 && ed2->Modified()))    // [case: 52158] close of modified file without save
						::InvalidateFileDateThread(pDocData->mNormalizedFilename); //  To cause a reparse next time the IDE is opened
					if (gVaService && !StopIt)                                     // don't do this at shutdown
					{
						DTypePtr fData = MultiParse::GetFileData(pDocData->mNormalizedFilename);
						if (!fData || !fData->IsSysLib()) // don't reparse system files
							g_ParserThread->QueueFile(pDocData->mNormalizedFilename);
					}
				}

				mLock.Lock();
			}

			gRsRunningDocuments.erase(it);
		}
	}

	{
		AutoLockCs l(g_EdCntListLock);
		for (auto it = g_EdCntList.begin(); it != g_EdCntList.end(); ++it)
		{
			HWND itWnd = (*it)->GetSafeHwnd();
			if (itWnd != mMainEditorWnd && !::IsWindow(itWnd))
			{
				EdCntPtr ed2 = *it;
				g_EdCntList.erase(it);
				RunningDocumentData::Release(ed2);
			}
		}
	}

	if (gVaService)
		gVaService->DocumentClosed(f);
}

void __stdcall VaRadStudioPlugin::EditorFileChanged(const wchar_t* AFileName, const char* AContent)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP EditorFileChanged: %s\n", AFileName);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	RunningDocumentDataPtr pDocData;
	UINT fid = gFileIdManager ? gFileIdManager->GetFileId(AFileName) : 0;
	if (fid)
	{
		AutoLockCs l(mLock);
		auto it = gRsRunningDocuments.find(fid);
		if (it != gRsRunningDocuments.end())
		{
			pDocData = it->second;
			_ASSERTE(pDocData);
			if (pDocData)
				pDocData->UpdateContent(AContent);
		}
	}

	if (gVaService && pDocData)
	{
#ifdef TRACE_HOST_EVENTS
		CStringW msg1;
		CString__FormatW(msg1, L"VARSP call DocumentModified: %s\n", AFileName);
		::OutputDebugStringW(msg1);
#endif
		gVaService->DocumentModified(pDocData->mNormalizedFilename, -1, -1, -1, -1, -1, -1, -1);

		EdCntPtr ed = g_currentEdCnt;
		if (ed && pDocData->IsConnected(ed))
		{
			CurrentEditorUpdated();

			if (gRadStudioHost)
				gRadStudioHost->OnEditorUpdated(ed.get());
		}
	}
}

// When loading a project group, this can fire before the ProjectGroupOpened notification.
// It does not fire for design or history view or leaving either to an already open view.
// No notification when switching between multiple views of the same file.
// No notification when switching between design and edit.
// No notification when doing File New Window.
// EditorViewActivated fires before or after EdCnt::OnSetFocus inconsistently (restore previous comment if Dave reverts latest change).
// EditorViewActivated will fire without OnSetFocus when switching between tabs -- editor focus doesn't change.
// With Dave's latest change, EditorViewActivated fires more often after OnSetFocus but still has
// missing notifications in multiple circumstances.
void __stdcall VaRadStudioPlugin::EditorViewActivated(const wchar_t* AFileName, HWND editorHandle)
{
#ifdef TRACE_HOST_EVENTS
	CStringW msg;
	CString__FormatW(msg, L"VARSP EditorViewActivated: %s\n", AFileName);
	::OutputDebugStringW(msg);
#endif

	if (!mInit)
		return;

	RunningDocumentDataPtr pDocData;

	{
		const UINT fid = gFileIdManager ? gFileIdManager->GetFileId(AFileName) : 0;
		mActiveFileId = fid;

		auto fit = mFileRSFrameworkType.find(fid);
		if (fit != mFileRSFrameworkType.cend())
			sActiveFramework = fit->second;
		else
			sActiveFramework = RsFrameworkType::None;

		AutoLockCs l(mLock);
		mLastActivatedFile = AFileName;
		auto it = gRsRunningDocuments.find(fid);
		if (it != gRsRunningDocuments.end())
			pDocData = it->second;

		if (!pDocData && fid &&
			//mLastActivatedFile == mLastAddedFile && 
			!::ShouldIgnoreFile(AFileName, true) &&
			::ShouldFileBeAttachedTo(AFileName))
		{
			// when a new file is created, activation comes before open notification:
			// 
			// VARSP FileAdded: C:\Users\sean.e\Documents\Embarcadero\Studio\Projects\File1.c
			// VARSP EditorViewActivated: C:\Users\sean.e\Documents\Embarcadero\Studio\Projects\File1.c
			// VA no doc data, reset for unsupported file
			// VARSP RsEditorDisconnected: C:\Users\sean.e\Documents\Embarcadero\Studio\Projects\Unit29.cpp
			// VARSP RsEditorConnected: unsupported.file
			// VARSP OnSetFocus: HWND(00021184) unsupported.file
			// VARSP FileOpenInEditor: C:\Users\sean.e\Documents\Embarcadero\Studio\Projects\File1.c
			// VARSP OnKillFocus: HWND(00021184) unsupported.file

			// workaround: pre-generate docData so that editor can be connected during 
			// FileOpenInEditor rather than EditorViewActivated
	
			pDocData = std::make_shared<RunningDocumentData>();
			pDocData->Init(fid, AFileName, nullptr);
			gRsRunningDocuments[fid] = pDocData;
		}

		mLastAddedFile.Empty();
	}

	// 	const HWND hFoc = ::GetFocus();
	// 	const CStringW wndClsName(::GetWindowClassString(hFoc).Wide());

	if (!mMainEditorWnd)
		mMainEditorWnd = LocateMainEditorWnd();

	HWND hEd = editorHandle; //GetActiveEditorHandle(); //nullptr;
	if (!hEd)
		hEd = mMainEditorWnd;

	// 	if (wndClsName == L"TEditControl")
// 	{
// 		hEd = hFoc;
// 	}
// 	else
// 	{
// 		hEd = ::LocateNewPopupEditor();
// 		if (!hEd && mMainEditorWnd)
// 			hEd = mMainEditorWnd;
// 		_ASSERTE(hEd);
// 	}

	EdCntPtr ed = nullptr;
	// see if already created, lookup EdCntPtr by hEd
	{
		AutoLockCs l(g_EdCntListLock);
		for (auto& it : g_EdCntList)
		{
			if (hEd == it->GetSafeHwnd())
			{
				ed = it;
				break;
			}
		}
	}

	if (ed)
	{
		OnScopeExit updateEd([&](){
			if (ed == g_currentEdCnt)
			{
#ifdef _DEBUG
				::OutputDebugStringW(L"#OUT EditorUpdate 1 \n");
#endif
				CurrentEditorUpdated();
			}
		});

		if (::ShouldFileBeAttachedTo(AFileName))
		{
			if (pDocData)
			{
				if (ed->FileName() == pDocData->mNormalizedFilename)
				{
#ifdef _DEBUG
					::OutputDebugStringW(L"VA maintain\n");
#endif
					{
						AutoLockCs l(mLock);
						if (!pDocData->IsConnected(ed))
							pDocData->Connect(ed);
					}

					if (g_currentEdCnt != ed)
						g_currentEdCnt = ed;
				}
				else
				{
#ifdef _DEBUG
					::OutputDebugStringW(L"VA recycle\n");
#endif
					RunningDocumentDataPtr pOldData;
					UINT oldFid = gFileIdManager ? gFileIdManager->GetFileId(ed->FileName()) : 0;
					AutoLockCs l(mLock);
					auto it = gRsRunningDocuments.find(oldFid);
					if (it != gRsRunningDocuments.end())
						pOldData = it->second;

					// if no data, then running documents table was likely cleared by EditorClosed
					if (pOldData && pOldData->IsConnected(ed))
					{
						pOldData->Disconnect(ed);
					}
					else if (pDocData->IsConnected(ed))
					{
						// renamed? don't save state -- push new state in Connect call that follows
						pDocData->DisconnectWithoutStateUpdate(ed);
					}

					pDocData->Connect(ed);
				}

				return;
			}
			else
			{
				// no doc data -- then we should not attach
#ifdef _DEBUG
				::OutputDebugStringW(L"VA no doc data, reset for unsupported file\n");
#endif
			}
		}
		else
		{
#ifdef _DEBUG
			::OutputDebugStringW(L"VA reset for unsupported file\n");
#endif
		}

		{
			AutoLockCs l(mLock);
			// reset for unsupported file
			mLastActivatedFile.Empty();
		}

		RunningDocumentDataPtr pOldData;
		{
			UINT oldFid = gFileIdManager ? gFileIdManager->GetFileId(ed->FileName()) : 0;
			AutoLockCs l(mLock);
			auto it = gRsRunningDocuments.find(oldFid);
			if (it != gRsRunningDocuments.end())
				pOldData = it->second;
			if (pOldData && pOldData->IsConnected(ed))
				pOldData->Disconnect(ed);
		}

		RunningDocumentData::ClearEdCnt(ed); // don't call while locked
		return;
	}

	if (hEd)
	{
		if (gVaAddinClient.AttachEditControl(hEd, AFileName, nullptr))
		{
#ifdef _DEBUG
			::OutputDebugStringW(L"VA activate attach succeed\n");
#endif
			{
				AutoLockCs l(g_EdCntListLock);
				for (auto& it : g_EdCntList)
				{
					if (hEd == it->GetSafeHwnd())
					{
						ed = it;
						break;
					}
				}
			}

			_ASSERTE(ed);
			if (ed && pDocData)
			{
				AutoLockCs l(mLock);
				_ASSERTE(!pDocData->IsConnected(ed));
				pDocData->Connect(ed);
			}
		}
		else
		{
#ifdef _DEBUG
			::OutputDebugStringW(L"VA activate attach FAIL\n");
#endif
			{
				AutoLockCs l(mLock);
				mLastActivatedFile.Empty();
			}
			_ASSERTE(!ShouldFileBeAttachedTo(AFileName));
			return;
		}
	}

	if (!mMainEdCnt && mMainEditorWnd)
	{
		ed = g_currentEdCnt;
		if (ed && ed->GetSafeHwnd() == mMainEditorWnd)
			mMainEdCnt = ed;
	}
}

#if !defined(PLUGIN_INTERFACE_COMMITTED_VERSION)

DWORD __stdcall VaRadStudioPlugin::QueryStatus(CommandTargetType cmdTarget, DWORD cmdId) const
{
	if (!mInit)
		return (DWORD)E_FAIL;
	if (!gVaService)
		return (DWORD)E_POINTER;
	return gVaService->QueryStatus((IVaService::CommandTargetType)cmdTarget, (DWORD)cmdId);
}

HRESULT __stdcall VaRadStudioPlugin::Exec(CommandTargetType cmdTarget, DWORD cmdId)
{
	if (!mInit)
		return (DWORD)E_FAIL;
	if (!gVaService)
		return (DWORD)E_POINTER;
	return gVaService->Exec((IVaService::CommandTargetType)cmdTarget, cmdId);
}

void __stdcall VaRadStudioPlugin::CommitSettings()
{
	// #cppbHostTODO host should call CommitSettings before displaying VA settings -- va should save settings for RS Options dlg
	_ASSERTE(Psettings);
	Psettings->Commit();
}

void __stdcall VaRadStudioPlugin::DocumentSaved(const wchar_t* filename)
{
	if (!mInit)
		return;

	// #cppbHostTODO host should call DocumentSaved after commit to disk -- the following block has not been compiled or tested
// 	RunningDocumentDataPtr pDocData;
// 	{
// 		AutoLockCs l(mLock);
// 		UINT fid = gFileIdManager ? gFileIdManager->GetFileId(filename) : 0;
// 		auto it = gRsRunningDocuments.find(fid);
// 		if (it != gRsRunningDocuments.end())
// 			pDocData = it->second;
// 
// 		if (!pDocData)
// 			return;
// 	}
// 
// 	for (const auto& it : pDocData->mConnectedEds)
// 		it->SendMessage(WM_VA_HANDLEKEY, ID_FILE_SAVE, 0);
}

#endif

static HWND LocateTEditControl(HWND hWnd)
{
	// 	  TEditWindow
	// 	     TPanel
	// 	       TPanel
	// 	         TPanel
	// 	           TPanel
	// 	             TPanel
	// 	               TPanel
	// 	                 TEditControl

	WTString wndClsName = ::GetWindowClassString(hWnd);
	if (wndClsName == "TEditWindow")
	{
		HWND chld3 = ::GetWindow(hWnd, GW_CHILD);
		while (chld3)
		{
			wndClsName = ::GetWindowClassString(chld3);
			if (wndClsName == "TPanel")
			{
				HWND chld4 = ::GetWindow(chld3, GW_CHILD);
				while (chld4)
				{
					wndClsName = ::GetWindowClassString(chld4);
					if (wndClsName == "TPanel")
					{
						HWND chld5 = ::GetWindow(chld4, GW_CHILD);
						while (chld5)
						{
							wndClsName = ::GetWindowClassString(chld5);
							if (wndClsName == "TPanel")
							{
								HWND chld6 = ::GetWindow(chld5, GW_CHILD);
								while (chld6)
								{
									wndClsName = ::GetWindowClassString(chld6);
									if (wndClsName == "TPanel")
									{
										HWND chld7 = ::GetWindow(chld6, GW_CHILD);
										while (chld7)
										{
											wndClsName = ::GetWindowClassString(chld7);
											if (wndClsName == "TPanel")
											{
												HWND chld8 = ::GetWindow(chld7, GW_CHILD);
												while (chld8)
												{
													wndClsName = ::GetWindowClassString(chld8);
													if (wndClsName == "TPanel")
													{
														HWND chld9 = ::GetWindow(chld8, GW_CHILD);
														while (chld9)
														{
															wndClsName = ::GetWindowClassString(chld9);
															if (wndClsName == "TEditControl")
															{
																return chld9;
															}

															chld9 = GetWindow(chld9, GW_HWNDNEXT);
														}
													}

													chld8 = GetWindow(chld8, GW_HWNDNEXT);
												}
											}

											chld7 = GetWindow(chld7, GW_HWNDNEXT);
										}
									}

									chld6 = GetWindow(chld6, GW_HWNDNEXT);
								}
							}

							chld5 = GetWindow(chld5, GW_HWNDNEXT);
						}
					}

					chld4 = GetWindow(chld4, GW_HWNDNEXT);
				}
			}

			chld3 = GetWindow(chld3, GW_HWNDNEXT);
		}
	}

	return nullptr;
}

HWND VaRadStudioPlugin::LocateMainEditorWnd()
{
	_ASSERTE(mMainWnd && !mMainEditorWnd);
	if (!mMainWnd)
		return nullptr;

	// 	TAppBuilder
	// 	  TPanel
	// 	    TEditorDockPanel
	// 	      TEditWindow
	// 	         TPanel
	// 	           TPanel
	// 	             TPanel
	// 	               TPanel
	// 	                 TPanel
	// 	                   TPanel
	// 	                     TEditControl

	WTString wndClsName(::GetWindowClassString(mMainWnd));
	if (wndClsName != "TAppBuilder")
		return nullptr;

	HWND chld = ::GetWindow(mMainWnd, GW_CHILD);
	while (chld)
	{
		wndClsName = ::GetWindowClassString(chld);
		if (wndClsName == "TPanel")
		{
			HWND chld1 = ::GetWindow(chld, GW_CHILD);
			while (chld1)
			{
				wndClsName = ::GetWindowClassString(chld1);
				if (wndClsName == "TEditorDockPanel")
				{
					HWND chld2 = ::GetWindow(chld1, GW_CHILD);
					while (chld2)
					{
						HWND tmp = LocateTEditControl(chld2);
						if (tmp)
							return tmp;

						chld2 = GetWindow(chld2, GW_HWNDNEXT);
					}
				}

				chld1 = GetWindow(chld1, GW_HWNDNEXT);
			}
		}

		chld = GetWindow(chld, GW_HWNDNEXT);
	}

	return nullptr;
}

static BOOL CALLBACK FindTEditEnumerationProc(HWND hWnd, LPARAM lParam)
{
	DWORD pid;
	GetWindowThreadProcessId(hWnd, &pid);
	if (pid != GetCurrentProcessId())
		return TRUE; // continue enumeration

	WTString wndClsName = GetWindowClassString(hWnd);
	if (wndClsName != "TEditWindow")
		return TRUE; // continue enumeration

	HWND tmp = LocateTEditControl(hWnd);
	if (!tmp)
		return TRUE; // continue enumeration

	bool alreadyAttached = false;
	{
		AutoLockCs l(g_EdCntListLock);
		for (auto it = g_EdCntList.begin(); it != g_EdCntList.end(); ++it)
		{
			HWND itWnd = (*it)->GetSafeHwnd();
			if (itWnd == tmp)
			{
				alreadyAttached = true;
				break;
			}
		}
	}

	if (!alreadyAttached)
	{
		*(HWND*)lParam = tmp;
		return FALSE; // break enumeration
	}

	return TRUE; // continue enumeration
}

// untested because we don't get an activation notification when user does File New Window
// (file is already activated (in a different editor) in order to execute the command)
static HWND LocateNewPopupEditor()
{
	// search process for a TEditControl that we haven't attached to yet
	// 	TAppBuilder
	//	Desktop
	// 	  TEditWindow -- owned by TAppBuilder
	// 	     TPanel
	// 	       TPanel
	// 	         TPanel
	// 	           TPanel
	// 	             TPanel
	// 	               TPanel
	// 	                 TEditControl


	HWND hWnd = nullptr;
	::EnumWindows(::FindTEditEnumerationProc, (LPARAM)&hWnd);
	return hWnd;
}

HWND VaRadStudioPlugin::GetActiveEditorHandle()
{
	int line = 0;
	int column = 0;
	HWND hWnd = nullptr;
	wchar_t buff[MAX_PATH * 8];
	
	if (gRadStudioHost->GetFocusedEditorCursorPos(buff, sizeof(buff), &line, &column, &hWnd))
	{
		return hWnd;
	}

	return nullptr;
}

bool VaRadStudioPlugin::IsVclFramework() const
{
	return sActiveFramework == RsFrameworkType::VCL;
}

bool VaRadStudioPlugin::IsFmxFramework() const
{
	return sActiveFramework == RsFrameworkType::FMX;
}

bool VaRadStudioPlugin::IsUsingFramework() const
{
	return IsFmxFramework() || IsVclFramework();
}

VaRadStudioPlugin::RsFrameworkType VaRadStudioPlugin::FrameworkType() const
{
	return sActiveFramework;
}

WTString
VaRadStudioPlugin::GetRunningDocText(const CStringW& filename) const
{
	UINT fid = gFileIdManager ? gFileIdManager->GetFileId(filename) : 0;
	if (!fid)
	{
		const CStringW f2 = ::NormalizeFilepath(filename);
		fid = gFileIdManager ? gFileIdManager->GetFileId(f2) : 0;
		if (!fid)
			return {};
	}

	RunningDocumentDataPtr pDocData;
	AutoLockCs l(mLock);
	auto it = gRsRunningDocuments.find(fid);
	if (it != gRsRunningDocuments.end())
	{
		pDocData = it->second;
		if (!pDocData->mBuffer.IsEmpty())
		{
			WTString ret = pDocData->mBuffer;
			RemovePadding_kExtraBlankLines(ret);
			return ret;
		}
	}

	return {};
}

// this was necessary when we had some issues with EdCnt::CheckForSaveAsOrRecycling
// causing state to change in unexpected ways.
// might not be necessary any more...
void VaRadStudioPlugin::UpdateEdCntConnection(EdCntPtr ed, const CStringW& newFile)
{
	CStringW lastActivatedFile;
	{
		AutoLockCs l(mLock);
		lastActivatedFile = mLastActivatedFile;
	}
	_ASSERTE(newFile == ::NormalizeFilepath(lastActivatedFile));

	RunningDocumentDataPtr pOldData;
	RunningDocumentDataPtr pDocData;

	{
		UINT oldFid = gFileIdManager ? gFileIdManager->GetFileId(ed->FileName()) : 0;
		UINT fid = gFileIdManager ? gFileIdManager->GetFileId(lastActivatedFile) : 0;

		AutoLockCs l(mLock);
		auto it = gRsRunningDocuments.find(oldFid);
		if (it != gRsRunningDocuments.end())
			pOldData = it->second;

		it = gRsRunningDocuments.find(fid);
		if (it != gRsRunningDocuments.end())
		{
			pDocData = it->second;
			if (pOldData && pOldData->IsConnected(ed))
				pOldData->Disconnect(ed);
		}
	}

	_ASSERTE(pDocData);
	if (pDocData)
	{
		AutoLockCs l(mLock);
		if (!pDocData->IsConnected(ed))
			pDocData->Connect(ed);
	}
	else
		RunningDocumentData::ClearEdCnt(ed); // don't call while locked

}

// fired when we notice we're loosing focus to a TEditControl
void VaRadStudioPlugin::NewEditorViewMaybeCreated()
{
	// based loosely on VaRadStudioPlugin::EditorViewActivated
	CStringW lastActivatedFile;
	{
		AutoLockCs l(mLock);
		lastActivatedFile = mLastActivatedFile;
	}
	if (lastActivatedFile.IsEmpty())
	{
		// in order for File New Window to be executed, the view
		// should have been activated.
		// if we no longer have the last activated file, then
		// we can't proceed here.
		return;
	}          

	HWND hEd = ::LocateNewPopupEditor();
	if (!hEd)
		return;

	RunningDocumentDataPtr pDocData;

	{
		UINT fid = gFileIdManager ? gFileIdManager->GetFileId(lastActivatedFile) : 0;
		AutoLockCs l(mLock);
		auto it = gRsRunningDocuments.find(fid);
		if (it != gRsRunningDocuments.end())
			pDocData = it->second;
	}

	if (!pDocData)
		return;

	if (gVaAddinClient.AttachEditControl(hEd, lastActivatedFile, nullptr))
	{
		EdCntPtr ed = nullptr;
		{
			AutoLockCs l(g_EdCntListLock);
			for (auto& it : g_EdCntList)
			{
				if (hEd == it->GetSafeHwnd())
				{
					ed = it;
					break;
				}
			}
		}

		_ASSERTE(ed); // not necessarily g_currentEdCnt at this point due to focus changing, etc
		if (ed)
		{
#ifdef TRACE_HOST_EVENTS
			CStringW msg;
			CString__FormatW(msg, L"VARSP NewEditorView: %s\n", (LPCWSTR)lastActivatedFile);
			::OutputDebugStringW(msg);
#endif
			AutoLockCs l(mLock);
			_ASSERTE(!pDocData->IsConnected(ed));
			pDocData->Connect(ed);
		}
	}
}

// internal notification via windows that an editor HWND is being closed
void VaRadStudioPlugin::EditorHwndClosing(HWND hEd)
{
	EdCntPtr ed;
	{
		AutoLockCs l(g_EdCntListLock);
		for (auto& it : g_EdCntList)
		{
			if (hEd == it->GetSafeHwnd())
			{
				ed = it;
				break;
			}
		}
	}

	if (!ed)
	{
		_ASSERTE(!"EditorHwndClosing notification but did not located EdCnt");
		return;
	}

	RunningDocumentDataPtr pDocData;
	UINT fid = gFileIdManager ? gFileIdManager->GetFileId(ed->FileName()) : 0;
	AutoLockCs l(mLock);
	auto it = gRsRunningDocuments.find(fid);
	if (it != gRsRunningDocuments.end())
		pDocData = it->second;

	if (pDocData && pDocData->IsConnected(ed))
		pDocData->Disconnect(ed);
}

void VaRadStudioPlugin::CurrentEditorUpdated()
{
	UpdateVaOutline();
}

void VaRadStudioPlugin::UpdateVaOutline()
{
	EdCntPtr ed = g_currentEdCnt;
	if (ed && gVaService)
	{
		auto *pOutline = gVaService->GetOutlineFrame();
		if (pOutline && pOutline->IsAutoUpdateEnabled())
		{
			pOutline->RequestRefresh(100u);
		}
	}
}

VaRadStudioPlugin::RsProjectDataPtr VaRadStudioPlugin::GetProjectData(uint idx)
{
	{
		AutoLockCs l(mLock);
		if (idx < mProjectGroup.size())
			return mProjectGroup[idx];
	}
	return std::make_shared<RsProjectData>();
}

void VaRadStudioPlugin::ParameterCompletionResult(VaRSParamCompletionData & paramData)
{
	_ASSERTE(g_mainThread == ::GetCurrentThreadId());
	if (gRadStudioHost)
	{
		auto errMsg = paramData.errorMessage.Wide();
		if (IsInParamIndexSelection())
		{
			//VADEBUGPRINT("#PCR ID sig: " << paramData.activeSig << " param: " << paramData.activeParam);

			gRadStudioHost->VAParameterIndexResult(
			    mParamIndexId,
			    paramData.activeSig,
			    paramData.activeParam,
			    !errMsg.IsEmpty(), 
				errMsg.IsEmpty() ? (LPCWSTR)nullptr : (LPCWSTR)errMsg);
		}
		else
		{
			//VADEBUGPRINT("#PCR PC sig: " << paramData.activeSig << " param: " << paramData.activeParam);

			gRadStudioHost->VAParameterCompletionResult(
			    mParamCompletionId,
			    errMsg.IsEmpty() ? paramData.Get() : nullptr,
			    errMsg.IsEmpty() ? paramData.Size() : 0,
			    paramData.activeSig,
			    paramData.activeParam,
			    !errMsg.IsEmpty(), 
				errMsg.IsEmpty() ? (LPCWSTR)nullptr : (LPCWSTR)errMsg);
		}
	}
}

void __stdcall VaRadStudioPlugin::ParameterCompletion(const wchar_t AFileName, int ALine, int ACharIndex, int AId)
{
	auto ed = g_currentEdCnt;
	if (ed)
	{
		new LambdaThread([=]() {
			::Sleep(50);
			RunFromMainThread([=]() {
				auto _this = gVaRadStudioPlugin;
				if (_this)
				{
					auto pos = LC_RAD_2_VA_POS(ALine, ACharIndex);
					long sp, ep;
					ed->GetSel(sp, ep);
					if (pos == sp)
					{
						TempAssign _tmp(_this->mParamCompletionId, AId, 0);
						ed->RsParamCompletion();
					}
				}
			}, false);
		}, "VAParameterCompletion", true);
	}
}

void __stdcall VaRadStudioPlugin::ParameterIndex(const wchar_t AFileName, int ALine, int ACharIndex, int AId)
{
	auto ed = g_currentEdCnt;
	if (ed)
	{
		new LambdaThread([=]() {
			::Sleep(50);
			RunFromMainThread([=]() {
				auto _this = gVaRadStudioPlugin;
				if (_this)
				{
					auto pos = LC_RAD_2_VA_POS(ALine, ACharIndex);
					long sp, ep;
					ed->GetSel(sp, ep);
					if (pos == sp)
					{
						TempAssign _tmp(_this->mParamIndexId, AId, 0);
						ed->RsParamCompletion();
					}
				}
			}, false);
		}, "VAParameterIndex", true);
	}
}

void __stdcall VaRadStudioPlugin::MenuUpdate(int AMenuID, bool* AVisible, bool* AEnabled, bool* AChecked)
{
	if (AVisible)
		*AVisible = false;

	if (AEnabled)
		*AEnabled = false;

	if (AChecked)
		*AChecked = false;

	auto menu = VaRSMenuManager::FindMenu(AMenuID);
	if (menu)
	{
		bool visible = false;
		bool enabled = false;
		bool checked = false;
		
		menu->UpdateState(visible, enabled, checked);
		
		if (AVisible)
			*AVisible = visible;

		if (AEnabled)
			*AEnabled = enabled;

		if (AChecked)
			*AChecked = checked;
	}
}

	 // Called when menu is executed
void __stdcall VaRadStudioPlugin::FeatureMenuExecute(int AMenu)
{
	AMenu = 0xFF & AMenu;

	VADEBUGPRINT("#RAD FeatureMenuExecute(" << AMenu << ")");

	auto menu = VaRSMenuManager::GetFeatureMenu((FeatureMenuId)AMenu);
	if (menu)
	{
		menu->Execute();
	}
}

// Called when the menu is pulled down or pops up
void __stdcall VaRadStudioPlugin::FeatureMenuUpdate(int AMenu,     // The menu id listed above
	bool* AVisible,           // If the item is shown or not
	bool* AChecked,           // If it is checked or not. Does not work on refactor menu
	bool* AEnabled) // Item is grayed if false
{
	AMenu = 0xFF & AMenu;

	VADEBUGPRINT("#RAD FeatureMenuUpdate(" << AMenu << ")");

	if (AVisible)
		*AVisible = false;

	if (AEnabled)
		*AEnabled = false;

	if (AChecked)
		*AChecked = false;

	auto menu = VaRSMenuManager::GetFeatureMenu((FeatureMenuId)AMenu);
	if (menu)
	{
		bool visible = false;
		bool enabled = false;
		bool checked = false;

		menu->UpdateState(visible, enabled, checked);

		if (AVisible)
			*AVisible = visible;

		if (AEnabled)
			*AEnabled = enabled;

		if (AChecked)
			*AChecked = checked;
	}
}

bool __stdcall VaRadStudioPlugin::FeatureMenuCaption(int AMenuID, /* The menu id to be changed */ const wchar_t* ATempCaption, /* The existing menu caption */ wchar_t** ANewCaption, /* The new caption */ bool* AAppend)
{
	VADEBUGPRINT("#RAD FeatureMenuCaption(" << AMenuID << ")");

	auto menu = VaRSMenuManager::GetFeatureMenu((FeatureMenuId)AMenuID);	
	if (menu)
	{
		bool append = false;
		auto caption = menu->UpdateCaption(append);
		if (caption)
		{
			*ANewCaption = (WCHAR*) caption;
			*AAppend = append;
			return true;
		}
	}
	return false;
}

void __stdcall VaRadStudioPlugin::MenuExecute(int AMenuID)
{
	VADEBUGPRINT("#RAD MenuExecute(" << AMenuID << ")");

	auto menu = VaRSMenuManager::FindMenu(AMenuID);
	if (menu)
	{
		menu->Execute();
	}
}

void __stdcall VaRadStudioPlugin::ParentChanging(int AWindowID)
{
	auto frame = VaRSFrameBase::RS_GetFrame(AWindowID);
	if (frame)
	{
		frame->RS_OnParentChanging();
	}
}

void __stdcall VaRadStudioPlugin::ParentChanged(int AWindowID, /* The ID passed in ShowWindow */ HWND ANewhWnd)
{
	auto frame = VaRSFrameBase::RS_GetFrame(AWindowID);
	if (frame)
	{
		frame->RS_OnParentChanged(ANewhWnd);
	}
}

void __stdcall VaRadStudioPlugin::InvokeCodeCompletion(
	const wchar_t * AFileName, /* The file where the completion request was made */ 
	int ALine, /* Line of cursor */ 
	int ACharIndex, /* Character index/ column of cursor */ 
	int AId)
{
	AssertRadLC(ALine, ACharIndex);

	auto ed = GetOpenEditWnd(AFileName);
	if (ed)
	{
		new LambdaThread([=]
		{ 
			RunFromMainThread([ed]() {
				ed->Reparse();
				ed->OnModified(TRUE);
 				ed->CurScopeWord();

// 				g_CompletionSet->ClearContents();
				g_CompletionSet->Dismiss();
				g_CompletionSet->UpdateCurrentPos(ed);
				g_CompletionSet->PopulateAutoCompleteData(ET_EXPAND_COMLETE_WORD /*ET_SUGGEST*/, false);
			});

			auto rsCompletion = VaRSCompletionData::Get();
			rsCompletion->Clear();

			auto count = (size_t)g_CompletionSet->GetCount();
			if (count)
			{
				rsCompletion->Init(count);

				WTString defaultDescr;

				for (size_t i = 0; i < count; i++)
				{
					symbolInfo* sinf = g_CompletionSet->m_ExpData.FindItemByIdx((int)i);
					if (sinf)
					{
						rsCompletion->Set(i, GetCompletionKind(sinf), sinf->mSymStr, defaultDescr);
					}
				}
			}

			RunFromMainThread([AId]() {
				auto rsCompletion = VaRSCompletionData::Get();
				auto ptr = rsCompletion->GetArray();
				if (ptr)
				{
					auto len = rsCompletion->GetLength();
					gRadStudioHost->VACodeCompletionResult(AId, ptr, len, false, nullptr);
				}
			});
			
		}, "VA_CodeCompletion", true);
	}

	// we need to let this return, then call
}

void __stdcall VaRadStudioPlugin::VAHelpInsight(int AIndex, wchar_t* AText, bool* AIsHTML)
{
	if (g_CompletionSet)
	{
		bool shouldColor = false;

		// [case: 164045] incorrect symbols in tooltips
		auto rsCompletion = VaRSCompletionData::Get();
		if (rsCompletion)
		{
			int itemIndex = rsCompletion->GetItemIndex((size_t)AIndex);
			if (itemIndex >= 0)
				AIndex = itemIndex; 
		}

		auto description = g_CompletionSet->GetDescriptionTextOrg(AIndex, shouldColor);
		auto wstr = description.Wide();
		wcscpy_s(AText, 4000, (LPCWSTR)wstr);
		*AIsHTML = false;
	}
	else
	{
		wcscpy_s(AText, 4000, L"");
		*AIsHTML = false;
	}
}

CStringW toolTipText;
extern CStringW GetCommentFromPos(int pos, int alreadyHasComment, int* commentFlags, EdCntPtr ed = {});
extern CStringW GetExtraDefInfoFromPos(int pos, EdCntPtr ed = {});
extern CStringW GetDefFromPos(int pos, EdCntPtr ed = {});

void __stdcall VaRadStudioPlugin::GetHintText(
    const wchar_t* AFileName, /* The file where the hint request was made */
    int ALine,                /* Line of cursor */
    int ACharIndex,           /* Character index/ column of cursor */
    int AId)
{
	auto ed = GetOpenEditWnd(AFileName);
	if (ed)
	{
		RunFromMainThread([=]() 
		{
			auto pos = LC_RAD_2_VA_POS(ALine, ACharIndex);

			TempSettingOverride<bool> ov1(&Psettings->m_AutoComments, true); // attempt to auto get comments			
			VaRSSelectionOverride _tso(ed, pos);

			auto defInfo = GetDefFromPos(pos, ed);

			gRadStudioHost->VAGetHintTextResult(AId, defInfo, false, nullptr);
		},
		false);
	}
}

void VaRadStudioPlugin::SaveRsProjectData(const wchar_t* AFileName, NsRadStudio::TProjectGroupData* AProjectGroupData)
{
	if (!AProjectGroupData)
		return;

	if (!AFileName)
		return;

	// project group (like solution): .groupproj
	// project: .cbproj
	mProjectGroupFile = AFileName;

	for (int projIdx = 0; projIdx < AProjectGroupData->FProjectCount; ++projIdx)
	{
		NsRadStudio::TFilesAndOptions* projIt = &AProjectGroupData->FFilesAndOptionsArray[projIdx];
		if (!projIt)
			continue;

		mProjectGroup.emplace_back(ReadRsProject(projIt));
	}
}

VaRadStudioPlugin::RsProjectDataPtr VaRadStudioPlugin::ReadRsProject(NsRadStudio::TFilesAndOptions* projIt)
{
	_ASSERTE(projIt);
	RsProjectDataPtr curProj = std::make_shared<RsProjectData>();
	curProj->mProject = projIt->FProjectName;
	curProj->mIncludeDirs = projIt->FInclude;
	curProj->SetFrameworkType(projIt->FFramework);

#ifdef TRACE_HOST_EVENTS
	{
		CStringW msg;
		CString__FormatW(msg, L"VARSP Reading project file: %s\n", (LPCWSTR)projIt->FProjectName);
		::OutputDebugStringW(msg);
	}
#endif

	if (curProj->mIncludeDirs.FindOneOf(L"%$") != -1)
	{
#ifdef TRACE_HOST_EVENTS
		CStringW msg;
		CString__FormatW(msg, L"ERROR: VARSP Project has unresolved env variable in include dir: %s\n", (LPCWSTR)curProj->mIncludeDirs);
		::OutputDebugStringW(msg);
#endif
	}

	const CStringW projectFilePath = ::Path(curProj->mProject);

	for (int sourceFileIdx = 0; sourceFileIdx < projIt->FFileCount; ++sourceFileIdx)
	{
		NsRadStudio::TFileAndOption* srcIt = &projIt->FFileAndOptionArray[sourceFileIdx];
		if (!srcIt || !srcIt->FFileName)
			continue;

		CStringW srcFile(::BuildPath(srcIt->FFileName, projectFilePath));
		if (srcFile.IsEmpty())
			srcFile = srcIt->FFileName;

		const UINT fid = gFileIdManager ? gFileIdManager->GetFileId(srcFile) : 0;
		mFileRSFrameworkType[fid] = curProj->mFrameworkType;
		if (mActiveFileId == fid)
			sActiveFramework = curProj->mFrameworkType;

		curProj->mSourceFiles.emplace_back(srcFile);
		if (srcIt->FLocalOptionInclude)
		{
			const CStringW srcFilePath(::Path(srcFile));
			CStringW localInclude(srcIt->FLocalOptionInclude);

			if (localInclude.FindOneOf(L"%$") != -1)
			{
#ifdef TRACE_HOST_EVENTS
				CStringW msg;
				CString__FormatW(msg, L"ERROR: VARSP Project has unresolved env variable in local include dir: %s\n", (LPCWSTR)localInclude);
				::OutputDebugStringW(msg);
#endif
			}

			if (localInclude[localInclude.GetLength() - 1] != L';')
				localInclude += L";";

			// iterate over localInclude paths to resolve dirs to relative srcIt->FFileName
			// (project loader will resolve relative to project dir, not source dir)
			SemiColonDelimitedString dirs(localInclude);
			while (dirs.HasMoreItems())
			{
				CStringW pth, pth2;
				dirs.NextItem(pth);
				pth2 = ::BuildPath(pth, srcFilePath, false, true);
				if (pth2.IsEmpty())
					pth2 = ::BuildPath(pth, projectFilePath, false, false);

				if (!pth2.IsEmpty())
					curProj->mFileIncludeDirs += pth2 + L";";
			}
		}
	}

	return curProj;
}

void VaRADStudioCleanup() 
{
	// you may want to use VaRadStudioPlugin::Shutdown instead of this
	// this method get called from VaApp::Exit before terminating _AtlModule
	VaRSMenuManager::Cleanup();
	VaRSCompletionData::Cleanup();

	extern VaRSParamCompletionData sRSParamCompletionData;
	sRSParamCompletionData.Clear();

	extern VaRSParamCompetionOverloads sRSParamComplOverloads; // see: #RAD_ParamCompletion
	sRSParamComplOverloads.Clear();
}

#endif