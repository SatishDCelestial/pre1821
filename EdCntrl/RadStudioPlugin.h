#pragma once

#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#define TRACE_HOST_EVENTS

#include <vector>
#include <memory>
#include "../CppBuilder/IVisualAssistPlugin.h"
#include "../CppBuilder/IRadStudioHost.h"
#include "Project.h"
#include "Lock.h"
#include "EdCnt_fwd.h"
#include "RadStudio/RadStudioUtils.h"
#include "RadStudio/RadStudioCompletion.h"

class VaRadStudioPlugin : public IVisualAssistPlugin
{
  public:
	VaRadStudioPlugin() = default;
	virtual ~VaRadStudioPlugin() = default;

	// public interface implementation
	void __stdcall InitializeVA(NsRadStudio::TInitializeData initData) override;
	void __stdcall Shutdown() override;

	void __stdcall ProjectGroupOpened(const wchar_t* AFileName, NsRadStudio::TProjectGroupData AProjectGroupData) override;
	void __stdcall ProjectGroupClosed() override;

	void __stdcall FileOpenInEditor(const wchar_t* AFileName, const char* AContent) override; // AContent is a utf8char *
	void __stdcall EditorClosed(const wchar_t* AFileName) override;
	void __stdcall EditorFileChanged(const wchar_t* AFileName, const char* AContent) override; // AContent is a utf8char *
	void __stdcall EditorViewActivated(const wchar_t* AFileName, HWND editorHandle) override;

	void __stdcall FindSymbolInProjectGroup() override;
	void __stdcall FindSymbolInProjectGroupUpdate(bool* LEnable, bool* LVisible) override;

	void __stdcall IDEThemeChanged() override;
	void __stdcall EnvOptionsChanged(const wchar_t* ASystemInclude) override;

	void __stdcall ProjectOptionsChanged(const wchar_t* AFileName, NsRadStudio::TFilesAndOptions* AFilesAndOptions) override;
	void __stdcall FileAdded(const wchar_t* AProjectName, const wchar_t* AFileName) override;
	void __stdcall FileRemoved(const wchar_t* AProjectName, const wchar_t* AFileName) override;
	void __stdcall FileRenamed(const wchar_t* AProjectName, const wchar_t* AOldFileName, const wchar_t* ANewFileName) override;
	void __stdcall ProjectAddedToGroup(const wchar_t* AProjectName, NsRadStudio::TFilesAndOptions* AFilesAndOptions) override;
	void __stdcall ProjectRemovedFromGroup(const wchar_t* AProjectName, NsRadStudio::TFilesAndOptions* AFilesAndOptions) override;
	void __stdcall ProjectRenamed(const wchar_t* AProjectName, const wchar_t* AOldFileName, const wchar_t* ANewFileName) override;
	void __stdcall MenuUpdate(int AMenuID, bool* AVisible, bool* AEnabled, bool* AChecked) override;
	void __stdcall MenuExecute(int AMenuID) override;

	  // Called when a parent of a hosted window is about to change
	void __stdcall ParentChanging(int AWindowID) override; // The ID passed in ShowWindow

	// Called when a parent of a hosted window has just changed
	void __stdcall ParentChanged(int AWindowID,  // The ID passed in ShowWindow
	                             HWND ANewhWnd) override; // The new parent window handle

	// Called when the data for a suggestion list or code completion is needed.
	// The results are returned by calling VACodeCompletionResult
	void __stdcall InvokeCodeCompletion(const wchar_t * AFileName, // The file where the completion request was made
	                                    int ALine,               // Line of cursor
	                                    int ACharIndex,          // Character index/ column of cursor
	                                    int AId) override;       // Identifier of this request must be returned to VACodeCompletionResult

	// Called when the IDE needs to show a tooltip information
	// The results are returned by calling VGetHintTextResult
	void __stdcall GetHintText(const wchar_t* AFileName, // The file where the hint request was made
	                           int ALine,                // Line of cursor
	                           int ACharIndex,           // Character index/ column of cursor
	                           int AId) override;        // Identifier of this request must be returned to VGetHintTextResult

	void __stdcall VAHelpInsight(int AIndex, wchar_t* AText, bool* AIsHTML) override;

		 // Called when menu is executed
	void __stdcall FeatureMenuExecute(int AMenu) override; // The menu id listed above

	// Called when the menu is pulled down or pops up
	void __stdcall FeatureMenuUpdate(int AMenu,                // The menu id listed above
	                                 bool* AVisible,           // If the item is shown or not
	                                 bool* AChecked,           // If it is checked or not. Does not work on refactor menu
	                                 bool* AEnabled) override; // Item is grayed if false

		bool __stdcall FeatureMenuCaption(int AMenu,                   // The menu id to be changed
	                                  const wchar_t* ATempCaption, // The existing menu caption
	                                  wchar_t** ANewCaption,       // The new caption
	                                  bool* AAppend) override;     // If this is set to true then

		void __stdcall ParameterCompletion(const wchar_t AFileName, int ALine, int ACharIndex, int AId) override;

	    void __stdcall ParameterIndex(const wchar_t AFileName, int ALine, int ACharIndex, int AId) override;


#if !defined(PLUGIN_INTERFACE_COMMITTED_VERSION)
	DWORD __stdcall QueryStatus(CommandTargetType cmdTarget, DWORD cmdId) const override;
	HRESULT __stdcall Exec(CommandTargetType cmdTarget, DWORD cmdId) override;

	void __stdcall CommitSettings() override;
	void __stdcall DocumentSaved(const wchar_t* filename) override;
#endif

	HWND GetActiveEditorHandle();

	// internal accessors
	CStringW GetBaseRegPath() const
	{
		AutoLockCs l(mLock);
		return mRegPath;
	}

	CStringW GetAppDataLocalPath() const
	{
		AutoLockCs l(mLock);
		return mAppDataLocalPath;
	}

	CStringW GetAppDataRoamingPath() const
	{
		AutoLockCs l(mLock);
		return mAppDataRoamingPath;
	}

	CStringW GetSystemIncludeDirs() const
	{
		AutoLockCs l(mLock);
		return mSysIncludeDirs;
	}

	CStringW GetSystemSourceDirs() const
	{
		AutoLockCs l(mLock);
		return mSysSourceDirs;
	}

	CStringW GetProjectGroupFile() const
	{
		AutoLockCs l(mLock);
		return mProjectGroupFile;
	}

	CStringW GetLastOpenedFile() const
	{
		AutoLockCs l(mLock);
		return mLastOpenedFile;
	}

	CStringW GetActiveFilename() const
	{
		AutoLockCs l(mLock);
		return mLastActivatedFile;
	}

	NsRadStudio::TTheme GetTheme() const
	{
		AutoLockCs l(mLock);
		return mCurTheme;
	}

	WTString GetRunningDocText(const CStringW& filename) const;
	void UpdateEdCntConnection(EdCntPtr ed, const CStringW& newFile);
	void NewEditorViewMaybeCreated();
	void EditorHwndClosing(HWND hEd);

	void CurrentEditorUpdated();
	void UpdateVaOutline();

	enum class RsFrameworkType
	{
		None,
		VCL,
		FMX,
		FMI,
		DotNet
	};
	
	RsFrameworkType FrameworkType() const;
	bool IsVclFramework() const;
	bool IsFmxFramework() const;
	bool IsUsingFramework() const;

	struct RsProjectData
	{
		CStringW mProject;
		CStringW mIncludeDirs;
		CStringW mFileIncludeDirs;
		std::vector<CStringW> mSourceFiles;
		RsFrameworkType mFrameworkType = RsFrameworkType::None;

		bool IsEmpty() const
		{
			_ASSERTE(!mProject.IsEmpty() || (mProject.IsEmpty() && mIncludeDirs.IsEmpty() && mFileIncludeDirs.IsEmpty() && mSourceFiles.empty()));
			return mProject.IsEmpty(); // && mIncludeDirs.IsEmpty() && mFileIncludeDirs.IsEmpty() && mSourceFiles.empty();
		}

		void SetFrameworkType(LPCWSTR fwType)
		{
			if (fwType == nullptr || !*fwType)
				mFrameworkType = RsFrameworkType::None;
			else if (!StrCmpIW(fwType, NsRadStudio::sFrameworkTypeVCL))
				mFrameworkType = RsFrameworkType::VCL;
			else if (!StrCmpIW(fwType, NsRadStudio::sFrameworkTypeFMX))
				mFrameworkType = RsFrameworkType::FMX;
			else if (!StrCmpIW(fwType, NsRadStudio::sFrameworkTypeFMI))
				mFrameworkType = RsFrameworkType::FMI;
			else if (!StrCmpIW(fwType, NsRadStudio::sFrameworkTypeDotNet))
				mFrameworkType = RsFrameworkType::DotNet;
			else
				mFrameworkType = RsFrameworkType::None;
		}

		bool ShouldIgnorePath(LPCWSTR fileName)
		{
			if (!fileName || !*fileName)
				return true;

			switch (mFrameworkType)
			{
			case RsFrameworkType::VCL:
				return !!StrStrIW(fileName, L"fmx");
			case RsFrameworkType::FMX:
				return !!StrStrIW(fileName, L"vcl");
			default:
				return false;
			}
		}

		bool HasDifference(const RsProjectData& other) const
		{
			if (mProject != other.mProject)
				return true;
			if (mIncludeDirs != other.mIncludeDirs)
				return true;
			if (mFileIncludeDirs != other.mFileIncludeDirs)
				return true;
			if (mSourceFiles != other.mSourceFiles)
				return true;
			return false;
		}
	};

	using RsProjectDataPtr = std::shared_ptr<RsProjectData>;
	uint GetProjectCount() const
	{
		// read not protected by lock due to use by FileParserWorkItem::CanRunNow() during project load
		return mProjectCount;
	}
	bool AreProjectsLoading() const
	{
		return mProjectsLoading;
	}
	RsProjectDataPtr GetProjectData(uint idx);

	void ParameterCompletionResult(VaRSParamCompletionData& paramData);
	bool IsInParamCompletion() const
	{
		return mParamCompletionId != 0;
	}
	bool IsInParamIndexSelection() const
	{
		return mParamIndexId != 0;
	}
	bool IsInParamCompletionOrIndexSelection() const
	{
		return IsInParamCompletion() || IsInParamIndexSelection();
	}

  private:
	void SaveRsProjectData(const wchar_t* AFileName, NsRadStudio::TProjectGroupData* AProjectGroupData);
	RsProjectDataPtr ReadRsProject(NsRadStudio::TFilesAndOptions* projIt);
	void NotifyProjectLoad();
	HWND LocateMainEditorWnd();

	bool mInit = false;
	HWND mMainWnd = nullptr;
	HWND mMainEditorWnd = nullptr;
	EdCntPtr mMainEdCnt;
	CStringW mRegPath;
	CStringW mAppDataRoamingPath;
	CStringW mAppDataLocalPath;
	CStringW mSysIncludeDirs;
	CStringW mSysSourceDirs;

	CStringW mLastOpenedFile;
	CStringW mLastActivatedFile;
	CStringW mLastAddedFile;

	mutable CCriticalSection mLock;
	using RsProjectGroup = std::vector<RsProjectDataPtr>;
	CStringW mProjectGroupFile;
	RsProjectGroup mProjectGroup;
	uint mProjectCount = 0;
	bool mProjectsLoading = false;
	std::map<UINT, RsFrameworkType> mFileRSFrameworkType;
	UINT mActiveFileId = 0;
	int mParamCompletionId = 0;
	int mParamIndexId = 0;

	NsRadStudio::TTheme mCurTheme;
};

extern VaRadStudioPlugin* gVaRadStudioPlugin;

#endif
