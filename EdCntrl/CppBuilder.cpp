#include "stdafxed.h"

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)
#include "CppBuilder.h"
#include "RadStudioPlugin.h"
#include "EDCNT.H"
#include "FILE.H"
#include "VaService.h"
#include "StringUtils.h"
#include "GetFileText.h"

IRadStudioHostEx* gRadStudioHost = nullptr;

/*
 * Jerry produced a prototype version of VA for RadStudio/C++Builder in Dec 2018.
 * He built a delphi plugin that was loaded by the host; the plugin loaded VA_X.dll.
 * The delphi project is in p4 at //ProductSource/VAProduct/VABuildTrunk/CppBuilder/VaDelphiPkg
 * The delphi plugin used windows messages to communicate with VA_X.
 * He modified VA to work in RadStudio in a hybrid MsDev/Devenv mode.
 * He said his idea was to treat RadStudio like VC6 (normal Win32, no managed UI) but with VA 
 * using VAssistNet.dll/VS messaging via a call to VaAddinClient::PostDefWindowProc at the end of
 * EdCnt::DefWindowProc and by setting MainWndH to the main editor HWND instead of the IDE.
 * The VS messaging was implemented on the host side in the Delphi plugin: 
 *		- see function MessageFromVA in VAInterop.pas for the stripped down pseudo-VAssistNet.dll
 *		- in Ter_mfc.cpp, he called CppBuilder in SendVamMessageToCurEd and CTer::SendVamMessage
 *		- CppBuilder was just an alias to the MessageFromVA delphi function
 *		- in this file (CppBuilder.cpp), he had MessageToVA which is how the delphi plugin talked to VA
 *
 * In VA Shell terms, RadStudio is:
	- CppBuilderShellAttributes : public Vc6ShellAttributes (not VC7+)
	- CppBuilderShellService : public Vc6ShellService (not VC7+)
	- !gShellAttr->IsMsdev()
    - gShellAttr->IsDevenv()
    - !gShellAttr->IsDevenv7()
	- !gShellAttr->IsDevenv8OrHigher()
    - gShellAttr->IsCppBuilder()
    - in RadStudio both gShellAttr->IsDevenv() and gShellAttr->IsCppBuilder() are true

	RadStudio is handled similar to VS7 with exceptions like:
		- EdCntCwnd (instead of EdCntCWndVs or EdCntWPF)
		- custom projects
		- custom system include handling
		- less editor integration
 *
 * The Delphi plugin, MessageFromVA and MessageToVA have been replaced with an 
 * interface implemented by the host (IRadStudioHost), and an interface implemented
 * by VA (IVisualAssistPlugin).  
 * MessageFromVA is replaced by ConvertMessageToInterfaceCall which in turn calls IRadStudioHost.
 * Rather than direct access to IRadStudioHost, VA uses a wrapper implemented below in RadStudioHostProxy.
 * 
 * The host gives VA a pointer to an implementation of IRadStudioHost when it
 * loads VA_X_RS.dll and calls its only export LoadVaPlugin implemented in this 
 * file.  VA returns its implementation of IVisualAssistPlugin.
 * 
 * 
 * General notes re: RadStudio editor functionality:
 *		supports: virtual space, block mode selection, code folding, zoom
 *		does not support: multi-select
 *
 * RadStudio HWND notes:
 *		IDE top-level wnd is TAppBuilder (parented to desktop with no owner)
 * 		Source text editors are TEditControl (parented in a hierarchy that includes TEditWindow but no owner)
 *		Top-level parent of pop-out editor is TEditWindow (parented to desktop but owned by the TAppBuilder;
 *			TEditWindow is normal in TEditControl hierarchy)
 *		Editor in TAppBuilder is a single TEDitControl whose content is swapped out and whose visibility is context-dependent
 *		
 */

#define USE_RAD_SELECTION_CACHE 0

// Wrapper around IRadStudioHost that forwards calls to the real host.
// Allows use of a wishlist interface before agreed upon and implemented by the host.
class RadStudioHostProxy : public IRadStudioHostEx
{
#if USE_RAD_SELECTION_CACHE
	HWND _selectionEditControl = nullptr;
	std::tuple<int, int, int, int> _radSelCache = std::make_tuple(0, 0, 0, 0); // startLine, startIndex, endLine, endIndex
	std::tuple<int, int, int, int> _vaSelection = std::make_tuple(0, 0, 0, 0);  // startLine, startIndex, endLine, endIndex
#endif

  public:
	  RadStudioHostProxy(IRadStudioHost* theHost) : mTheRealHost(theHost)
	{ }

	virtual ~RadStudioHostProxy() = default;

	void __stdcall OnEditorUpdated(EdCnt* ed) override
	{
#if USE_RAD_SELECTION_CACHE
		if (ed->m_hWnd == _selectionEditControl)
		{
			_vaSelection = std::make_tuple(0, 0, 0, 0);
			_radSelCache = std::make_tuple(0, 0, 0, 0);
			_selectionEditControl = nullptr;		
		}
#endif
	}

	const WCHAR* __stdcall ExGetActiveViewText(int* textLen) override
	{
		// #cppbHostTODO ::GetActiveViewText implemented in terms of gVaRadStudioPlugin
		const CStringW fname(gVaRadStudioPlugin->GetActiveFilename());
		if (!fname.IsEmpty())
		{
			static CStringW buf;
			buf = PkgGetFileTextW(fname);
			*textLen = buf.GetLength();
			return (LPCWSTR)buf;
		}

		return nullptr;
	}

	// #RAD_FakeSelection
	const WCHAR* __stdcall ExGetActiveViewSelectedText() override
	{
		static CStringW selStr;

// 		if (auto currRef = RadRefactoringOperation::Current(true))
// 		{
// 			auto ed = g_currentEdCnt;
// 			if (ed)
// 			{
// 				::SetActiveWindow(MainWndH);
// 				::SetFocus(ed->GetSafeHwnd());
// 				if (currRef->GetSelectedText(ed.get(), selStr))
// 				{
// 					return selStr;
// 				}
// 			}
// 		}

// 		int startLine = 0, startCharIndex = 0;
// 		int endLine = 0, endCharIndex = 0;
// 
// 		CStringW activeFile = gVaRadStudioPlugin->GetActiveFilename();
// 		gRadStudioHost->GetActiveViewSelectionRange(activeFile, &startLine, &startCharIndex, &endLine, &endCharIndex);
// 		int size = gRadStudioHost->TextRangeSize(activeFile, startLine, startCharIndex, endLine, endCharIndex);
// 		if (size)
// 		{
// 			std::vector<char> utf8buffer;
// 			utf8buffer.resize((size_t)size + 4, '\0');
// 			size = gRadStudioHost->GetActiveViewText(activeFile, &utf8buffer.front(), size, startLine, startCharIndex, endLine, endCharIndex);
// 			if (size)
// 			{
// 				selStr = ::MbcsToWide(&utf8buffer.front(), size);
// 				return selStr;
// 			}
// 		}

		// #cppbHostTODO host needs to implement GetActiveViewSelectedText
		return nullptr;
	}

	// #RAD_FakeSelection
	void __stdcall ExReplaceActiveViewSelectedText(const WCHAR* text, int reformat) override
	{
		UNREFERENCED_PARAMETER(text);
		UNREFERENCED_PARAMETER(reformat);


// 		int startLine = 0, startCharIndex = 0;
// 		int endLine = 0, endCharIndex = 0;

// 		CStringW activeFile = gVaRadStudioPlugin->GetActiveFilename();
// 		gRadStudioHost->GetActiveViewSelectionRange(activeFile, &startLine, &startCharIndex, &endLine, &endCharIndex);
// 		RadWriteOperation writer(activeFile);
// 		writer.ReplaceTextRAD(startLine, startCharIndex, endLine, endCharIndex, text);
	}

	// #RAD_FakeSelection
	void __stdcall ExGetActiveViewSelection(int* startLine, int* startCol, int* endLine, int* endCol) override
	{
		// #cppbHostTODO host needs to implement GetActiveViewSelection
		*startLine = *startCol = *endLine = *endCol = 0;

// 		CStringW activeFile = gVaRadStudioPlugin->GetActiveFilename();
// 		gRadStudioHost->GetActiveViewSelectionRange(activeFile, startLine, startCol, endLine, endCol);


//#ifdef _DEBUG
		wchar_t buff[MAX_PATH * 8];
		memset(buff, 0, sizeof(buff));
		int line = 0, column = 0;
		HWND hWnd = nullptr;
		if (gRadStudioHost->GetFocusedEditorCursorPos(buff, sizeof(buff), &line, &column, &hWnd))
		{
			*startLine = line;
			*startCol = column;
			*endLine = line;
			*endCol = column;
		}
//#endif
	}

	// #RAD_FakeSelection
	void __stdcall ExSetActiveViewSelection(int startLine, int startCol, int endLine, int endCol) override
	{
		CStringW activeFile = gVaRadStudioPlugin->GetActiveFilename();

		if (activeFile.IsEmpty())
			activeFile = ExGetActiveViewFilename();

		if (endLine < startLine || (endLine == startLine && endCol < startCol))
		{
			std::swap(startLine, endLine);
			std::swap(startCol, endCol);
		}
		
		gRadStudioHost->LoadFileAtLocation(activeFile, startLine, startCol, endLine, endCol);
	}

	const WCHAR* __stdcall ExGetActiveViewFilename() override
	{
		static CStringW buf;

		buf = gVaRadStudioPlugin->GetActiveFilename();

#ifdef DEBUG
		int line = 0;
		int column = 0;
		HWND hWnd = nullptr;
		wchar_t buff[MAX_PATH * 2];
		if (gRadStudioHost->GetFocusedEditorCursorPos(buff, sizeof(buff), &line, &column, &hWnd))
		{
			auto x = ::NormalizeFilepath(buf);
			auto y = ::NormalizeFilepath(buff);
			ASSERT(_wcsicmp(x, y) == 0);
		}
#endif
		return (LPCWSTR)buf;
	}

	void __stdcall LoadFileAtLocation(const wchar_t* AFileName, int AStartLine, int AStartCharIndex, int AEndLine, int AEndCharIndex) override
	{
		if (mTheRealHost)
		{
			if (AStartLine == 0)
				AStartLine++;

			if (AEndLine == 0)
				AEndLine++;

			mTheRealHost->LoadFileAtLocation(AFileName, AStartLine, AStartCharIndex, AEndLine, AEndCharIndex);
		}
	}

	HWND __stdcall ShowWindow(int AWindowID, const wchar_t* ACaption) override // in - The caption for the window in the IDE
	{
		if (mTheRealHost)
		{
			return mTheRealHost->ShowWindow(AWindowID, ACaption);
		}
		return nullptr;
	}

	// This is the callback that returns the suggestion list or code completion list.
	// This is called after InvokeCodeCompletion request.
	// NOTE: you can not call this function during execution of InvokeCodeCompletion.
	// You must allow InvokeCodeCompletion to return and finish processing data. This
	// is not an issue with asynchronous implementation but if it is not async, you must
	// do a post message to yourself or something to allow InvokeCodeCompletion to finish
	// before calling VACodeCompletionResult
	void __stdcall VACodeCompletionResult(int AId,                           // id that was passed in InvokeCodeCompletion
	                                      const TCompletionItem* ACompletionArray, // array of suggestion items that will be shown to the user
	                                      int ALength,                       // number of elements in the array
	                                      bool AError,                       // set to true if there was an error
	                                      const wchar_t* AMessage) override  // the error message only used if AError is true
	{
		if (mTheRealHost)
		{
			mTheRealHost->VACodeCompletionResult(AId, ACompletionArray, ALength, AError, AMessage);
		}
	}

	// This is the callback that returns the suggestion list or code completion list.
	// This is called after InvokeCodeCompletion request.
	// NOTE: you can not call this function during execution of InvokeCodeCompletion.
	// You must allow InvokeCodeCompletion to return and finish processing data. This
	// is not an issue with asynchronous implementation but if it is not async, you must
	// do a PostMessage to yourself or something to allow InvokeCodeCompletion to finish
	// before calling VACodeCompletionResult
	void __stdcall VAGetHintTextResult(int AId,                          // id that was passed in when tooltip is invoked
	                                   const wchar_t* AText,     // The text that will be displayed in the tooltip
	                                   bool AError,              // set to true if there was an error
	                                   const wchar_t* AMessage) override // the error message only used if AError is true
	{
		if (mTheRealHost)
		{
			mTheRealHost->VAGetHintTextResult(AId, AText, AError, AMessage);
		}
	}

		// Inserts the given text AText at the location ALine and ACharIndex. This call
	// must be made between BeginWrite and EndWrite
	void __stdcall WriteToFile(int ALine,          // The line the text will be inserted
	                           int ACharIndex,     // The char index of the text to be inserted
	                           const char* AText) override // The text to be insert. (This text is UTF8)
	{
		if (mTheRealHost)
		{
			mTheRealHost->WriteToFile(ALine, ACharIndex, AText);
		}	
	}

	// Deletes the text in the ramge of ALine, ACharIndex and AEndLine, AEndCharIndex
	// This call must be made between BegineWrite and EndWrite
	void __stdcall DeleteFromFile(int ALine,          // Starting line of the text to be deleted
	                              int ACharIndex,     // Starting CharIndex of the text to be deleted
	                              int AEndLine,       // Ending line of the text to be deleted
	                              int AEndCharIndex) override // Ending CharIndex of the text to be deleted
	{
		if (mTheRealHost)
		{
			mTheRealHost->DeleteFromFile(ALine, ACharIndex, AEndLine, AEndCharIndex);
		}	
	}

	// Starts a Write or Delete operations. This will also start an undo block. The
	// file specified is the file that is to be changed
	void __stdcall BeginWrite(const wchar_t* AFileName) override // The file name to be edited
	{
		if (mTheRealHost)
		{
			mTheRealHost->BeginWrite(AFileName);
		}	
	}

	// Ends the Write or Delete operation. When this is called all Writes and Deletes
	// are committed to the file and the undo block is closed.
	void __stdcall EndWrite() override
	{
		if (mTheRealHost)
		{
			mTheRealHost->EndWrite();
		}
	}

		// Returns the starting and ending position of selected text. If no text is
	// selected or if the file is not found then all zeros will be returned.
	// The top most view of the file name provided will be used in the case of
	// multiple views have the same file open
	void __stdcall GetActiveViewSelectionRange(const wchar_t* AFileName, // Name of file to check for selection
	                                           int* AStartLine,          // starting line of selection
	                                           int* AStartCharIndex,     // starting char index of selection
	                                           int* AEndLine,            // ending line of selection
	                                           int* AEndCharIndex) override // ending char index of selection
	{
		if (mTheRealHost)
		{
			mTheRealHost->GetActiveViewSelectionRange(AFileName, AStartLine, AStartCharIndex, AEndLine, AEndCharIndex);
		}
	}
	
																			// Copies text from the range provided to ABuffer. Up to ABufferSize bytes
	// are copied and the total number of bytes copied are returned. If the file is
	// not found or if positions provide are not found then zero is returned.
	int __stdcall GetActiveViewText(const wchar_t* AFileName, // Name of file of bytes to be read
	                                char* ABuffer,            // An allocated utf8 buffer
	                                int ABufferSize,          // Max number of bytes to be copied
	                                int AStartLine,           // starting line of bytes to be copied
	                                int AStartCharIndex,      // starting char index of bytes to be copied
	                                int AEndLine,             // ending line of bytes to be copied
	                                    int AEndCharIndex) override // ending char index of bytes to be copied
	{
		if (mTheRealHost)
		{
			return mTheRealHost->GetActiveViewText(AFileName, ABuffer, ABufferSize, AStartLine, AStartCharIndex, AEndLine, AEndCharIndex);
		}

		return -1;
	}

	// Returns number of bytes needed to store selected range for given file. This
	// does NOT include a zero terminator. If the file is not found or the range is
	// out of range of the file then zero is returned.
	int __stdcall TextRangeSize(const wchar_t* AFileName, // Name of file to get number of bytes
	                            int AStartLine,           // Starting line of size to be calcualted
	                            int AStartCharIndex,      // Starting char index of size to be calcualted
	                            int AEndLine,             // Ending line of size to be calcualted
	                                int AEndCharIndex) override // Ending char index of size to be calcualted
	{
		if (mTheRealHost)
		{
			return mTheRealHost->TextRangeSize(AFileName, AStartLine, AStartCharIndex, AEndLine, AEndCharIndex);
		}

		return -1;
	}

	
	void __stdcall VAParameterCompletionResult(int AId, TSignatureInfo* AParameterArray, int ALength, int AActiveSignature, int AActiveParameter, bool AError, const wchar_t* AMessage) override
	{
		if (mTheRealHost)
		{
			mTheRealHost->VAParameterCompletionResult(AId, AParameterArray, ALength, AActiveSignature, AActiveParameter, AError, AMessage);
		}
	}

	void __stdcall VAParameterIndexResult(int AId, int AActiveSignature, int AActiveParameter, bool AError, const wchar_t* AMessage) override
	{
		if (mTheRealHost)
		{
			mTheRealHost->VAParameterIndexResult(AId, AActiveSignature, AActiveParameter, AError, AMessage);
		}
	}


	void __stdcall ExFormatActiveViewSelectedText() override
	{
		// #cppbHostTODO host needs to implement FormatActiveViewSelectedText
	}

	void __stdcall ExSelectAllInActiveView() override
	{
		// #cppbHostTODO host needs to implement SelectAllInActiveView
	}

	void __stdcall ExScrollLineToTopInActiveView() override
	{
		// #cppbHostTODO host needs to implement ScrollLineToTopInActiveView
	}

	void __stdcall ExSwapAnchorInActiveView() override
	{
		// #cppbHostTODO host needs to implement SwapAnchorInActiveView or implement in terms of SetActiveViewSelection
	}

	int __stdcall ExActiveViewHasBlockOrMultiSelect() override
	{
		// #cppbHostTODO host needs to implement ActiveViewHasBlockOrMultiSelect
		return false;
	}

	int __stdcall ExGetActiveViewFirstVisibleLine() override
	{
		// #cppbHostTODO host needs to implement GetActiveViewFirstVisibleLine
		return 0;
	}

	void __stdcall ExLaunchHelp(const wchar_t* helpTopic) override
	{
		// #cppbHostTODO host needs to implement LaunchHelp
		UNREFERENCED_PARAMETER(helpTopic);
	}

	bool __stdcall GetTheming(NsRadStudio::TTheme* ATheme) override
	{
		if (mTheRealHost)
			return mTheRealHost->GetTheming(ATheme);
		return false;
	}

	bool __stdcall GetSystemColor(int AColor, int* ANewColor) override
	{
		if (mTheRealHost)
			return mTheRealHost->GetSystemColor(AColor, ANewColor);
		return false;
	}

	bool __stdcall AddMainMenuItem(const wchar_t* ACaption, int AMenuID, int AInsertAfter, int IAParent) override
	{
		if (mTheRealHost)
			return mTheRealHost->AddMainMenuItem(ACaption, AMenuID, AInsertAfter, IAParent);

		return false;
	}

	bool __stdcall AddSearchMenuItem(const wchar_t* ACaption, int AMenuID, int AInsertAfter, int IAParent) override
	{
		if (mTheRealHost)
			return mTheRealHost->AddSearchMenuItem(ACaption, AMenuID, AInsertAfter, IAParent);

		return false;
	}

	bool __stdcall AddLocalMenuItem(const wchar_t* ACaption, int AMenuID, int AInsertAfter, int IAParent) override
	{
		if (mTheRealHost)
			return mTheRealHost->AddLocalMenuItem(ACaption, AMenuID, AInsertAfter, IAParent);

		return false;
	}

	bool __stdcall AddFeatureMenu(FeatureMenuId AMenu) override
	{
		if (mTheRealHost)
			return mTheRealHost->AddFeatureMenu(AMenu);

		return false;
	}

	bool __stdcall GetFocusedEditorCursorPos(const wchar_t* AFileName, int AFileLen, int* ALine, int* AColumn, HWND* AhWnd) override
	{
		if (mTheRealHost && mTheRealHost->GetFocusedEditorCursorPos(AFileName, AFileLen, ALine, AColumn, AhWnd))
		{
			return true;
		}

		return false;
	}

	// Returns true for given if the give file is using column marking and false if
	// not.
	bool __stdcall IsColumnMark(const wchar_t* AFileName) override // AFileName is the file to check for column marking
	{
		if (mTheRealHost)
			return mTheRealHost->IsColumnMark(AFileName);
		return false;
	}

	// Starts notifications to the status bar in the project manager
	void __stdcall ShowStatusBegin(const wchar_t* AToken,  // A unique string that identifies these updates
	                               const wchar_t* ATitle) override // The string that is shown in the project manager
	{
		if (mTheRealHost)
			mTheRealHost->ShowStatusBegin(AToken, ATitle);
	}
	// Shows any status updates for a given token
	void __stdcall ShowStatusReport(const wchar_t* AToken,   // A unique string that identifies these updates
	                                const wchar_t* AMessage, // The string that is shown in the project manager
	                                    int APercentage) override // Shows the percentage of completion in project manager if -1 no percentage is shown
	{
		if (mTheRealHost)
			mTheRealHost->ShowStatusReport(AToken, AMessage, APercentage);
	}
	// Ends the status updating
	void __stdcall ShowStatusEnd(const wchar_t* AToken,    // A unique string that identifies these updates
	                                 const wchar_t* AMessage) override // A Message of completion
	{
		if (mTheRealHost)
			mTheRealHost->ShowStatusEnd(AToken, AMessage);
	}
	// Returns the number of bytes needed to store the file in utf8 characters plus the terminator
	    int __stdcall GetFileContentLength(const wchar_t* AFileName) override // Fully qualified name of file
	{
		if (mTheRealHost)
			return mTheRealHost->GetFileContentLength(AFileName);
		return 0;
	}
	// Copies the contents of the file AFileName to ABuffer. GetFileContentLength
	// must be called before calling this.
	bool __stdcall GetFileContent(const wchar_t* AFileName, // Fully qualified name of file
	                              const char* ABuffer,      // pointer to the buffer allocated in VA
	                                  int ALength) override     // The length of the buffer
	{
		if (mTheRealHost)
			return mTheRealHost->GetFileContent(AFileName, ABuffer, ALength);
		return false;
	}
	// Shows the string AMessage in the message view in the IDE under a VAssist tab
	void __stdcall ShowInMessageView(const wchar_t* AMessage) override // The message to be shown
	{
		if (mTheRealHost)
			return mTheRealHost->ShowInMessageView(AMessage);
	}

	int __stdcall NeedToUpdateDesigner(const wchar_t* AFileName, const char* AName) override
	{
		if (mTheRealHost)
			return mTheRealHost->NeedToUpdateDesigner(AFileName, AName);
		return false;
	}

	bool __stdcall RenameDesignerItem(const wchar_t* AFileName, const char* AName, const char* ANewName) override
	{
 		if (mTheRealHost)
 			return mTheRealHost->RenameDesignerItem(AFileName, AName, ANewName);
		return false;
	}

	// This function returns true if the fully qualifed file name is loaded in the editor.
	// A virtual file (not store on disk) or a regular file loaded in the editor will return true even
	// if an editor tab is not shown in the editor. This function is more efficient than calling
	// the function GetFileContentLength.
	bool __stdcall IsFileInEditor(const wchar_t* AFileName) override // The full qualifed file name to check
	{
		if (mTheRealHost)
			return mTheRealHost->IsFileInEditor(AFileName);
		return false;
	}

	// Disables Optimal Fill option in editor if enabled. Most call RestoreOptimalFill to restore it state.
	// This should be called if you do not want the possible of the IDE changing spaces to tabs when the
	// the cursor is moved.
	void __stdcall DisableOptimalFill() override
	{
		if (mTheRealHost)
			return mTheRealHost->DisableOptimalFill();
	}

	// Called after DisableOptimalFill to restore its state
	void __stdcall RestoreOptimalFill() override
	{
		if (mTheRealHost)
			return mTheRealHost->RestoreOptimalFill();
	}

	void __stdcall ExOpenUndoBlock() override
	{
		// #cppbHostTODO host needs to implement OpenUndoBlock -- not yet in use by VA
	}

	void __stdcall ExCloseUndoBlock() override
	{
		// #cppbHostTODO host needs to implement CloseUndoBlock -- not yet in use by VA
	}

	void __stdcall ExSetModal(bool isModal) override
	{
		// #cppbHostTODO host needs to implement SetModal -- not yet in use by VA
	}

	bool __stdcall ExGetSelectionRangeForVA(EdCnt * ed, int* startLine, int* startCol, int* endLine, int* endCol) override
	{
		*startLine = *startCol = *endLine = *endCol = 1;

		if (!mTheRealHost)
			return false;

		auto fileName = ed->FileName();
		GetActiveViewSelectionRange(fileName, startLine, startCol, endLine, endCol);
		if (*startLine == 0 || *endLine == 0)
		{
			// clear selection cache
#if USE_RAD_SELECTION_CACHE
			_selectionEditControl = nullptr;
			_radSelCache = std::make_tuple(0, 0, 0, 0);
			_vaSelection = std::make_tuple(0, 0, 0, 0);
#endif

			HWND hWnd = nullptr;
			if (!GetFocusedEditorCursorPos(nullptr, 0, startLine, startCol, &hWnd))
			{
				*startLine = *startCol = *endLine = *endCol = 1;
				return false;
			}

			_ASSERT(ed->m_hWnd == hWnd);

			if (ed->m_hWnd == hWnd)
			{
				LC_RAD_2_VA(startLine, startCol);
				*endLine = *startLine;
				*endCol = *startCol;

				return true;			
			}

			return false;
		}

#if USE_RAD_SELECTION_CACHE
		if (_selectionEditControl == ed->m_hWnd)
		{
			if (std::get<0>(_radSelCache) == *startLine && 
				std::get<1>(_radSelCache) == *startCol &&
				std::get<2>(_radSelCache) == *endLine &&
				std::get<3>(_radSelCache) == *endCol)
			{
				*startLine = std::get<0>(_vaSelection);
				*startCol = std::get<1>(_vaSelection);
				*endLine = std::get<2>(_vaSelection);
				*endCol = std::get<3>(_vaSelection);
				return true;
			}
		}

		// clear selection cache
		_selectionEditControl = nullptr;
		_radSelCache = std::make_tuple(0, 0, 0, 0);
		_vaSelection = std::make_tuple(0, 0, 0, 0);

		int tmpSL = *startLine, tmpSC = *startCol;
		int tmpEL = *endLine, tmpEC = *endCol;
#endif

		// startCol and endCol and UTF8 positions from the start of the line
		// we need to recalculate those to UTF16 based line and column

		auto buf = ed->GetBufConst();
		int startLineStart = ed->GetBufIndex(buf, (long)ed->LinePos(*startLine));
		bool startEqualsEnd = *startLine == *endLine && *startCol == *endCol;

		*startCol = CharIndexUTF8ToUTF16(buf, startLineStart, startLineStart + *startCol);
		if (*startCol <= 0)
		{
			_ASSERTE(!"Invalid Line/CharIndex result 1");
			*startCol = 1;
			return false;		
		}

		if (startEqualsEnd)
		{
			*endCol = *startCol;
		}
		else
		{
			int endLineStart = ed->GetBufIndex(buf, (long)ed->LinePos(*endLine));
			*endCol = CharIndexUTF8ToUTF16(buf, endLineStart, endLineStart + *endCol);
			if (*endCol <= 0)
			{
				_ASSERTE(!"Invalid Line/CharIndex result 2");
				*endCol = 1;
				return false;
			}		
		}

#if USE_RAD_SELECTION_CACHE
		// update selection cache
		_selectionEditControl = ed->m_hWnd;
		_radSelCache = std::make_tuple(tmpSL, tmpSC, tmpEL, tmpEC);
		_vaSelection = std::make_tuple(*startLine, *startCol, *endLine, *endCol);
#endif
		return true;
	}

  private:
	IRadStudioHost *mTheRealHost = nullptr;
};

extern "C" _declspec(dllexport) void* LoadVaPlugin(void* host)
{
	// gRadStudioHost = static_cast<IRadStudioHost*>(host);
	gRadStudioHost = new RadStudioHostProxy(static_cast<IRadStudioHost*>(host));

	gVaRadStudioPlugin = new VaRadStudioPlugin{};
	return (IVisualAssistPlugin*)gVaRadStudioPlugin;
}

#endif // RAD_STUDIO