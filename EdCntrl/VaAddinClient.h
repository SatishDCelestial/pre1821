#pragma once

#include "IVaVsAddinClient.h"
#include "IVaLicensing.h"

// interface for vassistnet.dll and vassist.dll to call into va_x.
// The implementation of the class is littered throughout va_x where the
// original C functions have always been.
class VaAddinClient : public IVaVsAddinClient, public IVaLicensingUi
{
	bool mLicenseInit = false;
	std::atomic<int> mSetupAttemptedBeforeInit = 0;
	std::atomic<int> mDteExecutingCommandsCount = 0;
	std::mutex mDteExecutingCommandsMutex;
	std::vector<std::string> mDteExecutingCommands;
	CComPtr<IUnknown> mServiceProviderUnk;

  public:
	VaAddinClient() = default;
	virtual ~VaAddinClient() = default;

	void LicenseInitComplete()
	{
		mLicenseInit = true;
	}
	bool HasAddinAlreadyTriedSetup() const
	{
		return mSetupAttemptedBeforeInit != 0;
	}

	bool IsExecutingDteCommand()
	{
		return mDteExecutingCommandsCount > 0;
	}

	std::string GetExecutingDteCommands()
	{
		std::string rslt;

		std::lock_guard<std::mutex> lck(mDteExecutingCommandsMutex);
		for (const auto& x : mDteExecutingCommands)
		{
			if (!rslt.empty())
				rslt += ';';

			rslt += x;
		}

		return rslt;
	}

	// IVaVsAddinClientBase
	virtual BOOL SetupVa(IUnknown* serviceProviderUnk) override;
	virtual void SetMainWnd(HWND h) override;
	virtual tagSettings* GetSettings() override;
	virtual void SettingsUpdated(DWORD option) override;
	virtual void Shutdown() override;
	virtual HWND AttachEditControl(HWND hMSDevEdit, const WCHAR* file, void* pDoc) override;
	virtual LRESULT PreDefWindowProc(UINT, WPARAM, LPARAM) override;
	virtual LRESULT PostDefWindowProc(UINT, WPARAM, LPARAM) override;

	// IVaLicensingUi
	virtual void LicenseInitFailed() override;
	virtual BOOL DoTryBuy(int licenseStatus, bool nonPerpetualLicense) override;
	virtual int ErrorBox(const char* msg, unsigned int options) override;
	virtual void GetBuildDate(int& buildYr, int& buildMon, int& buildDay) override;
	virtual void StartLicenseMonitor() override;

	// IVaVsAddinClient
	virtual BOOL CheckSolution() override;
	virtual BOOL LoadSolution() override;
	virtual void AddFileToProject(const WCHAR* pProject, const WCHAR* pfile, BOOL nonbinarySourceFile) override;
	virtual void RemoveFileFromProject(const WCHAR* pProject, const WCHAR* pfile, BOOL nonbinarySourceFile) override;
	virtual void RenameFileInProject(const WCHAR* pProject, const WCHAR* pOldfilename,
	                                 const WCHAR* pNewfilename) override;
	virtual int GetTypingDevLang() override;
	virtual void ExecutingDteCommand(int execing, LPCSTR command) override;

	// IVaVcAddinClient
	virtual void LoadWorkspace(const WCHAR* projFiles) override;
	virtual void CloseWorkspace() override;
	virtual void SaveBookmark(LPCWSTR filename, int lineNo, BOOL clearAllPrevious) override;
	virtual void CheckForLicenseExpirationIfEnabled() override;
};

extern VaAddinClient gVaAddinClient;
