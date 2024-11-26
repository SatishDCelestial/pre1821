#pragma once

// original intent of breaking out IVaLicensingUi and IVaLicensing was
// that they could be implemented in separate dlls.  This was considered
// at one point due to problems we encountered with Armadillo post-processing
// step on VA_X.dll that caused problems with more recent version of Visual
// Studio (and maybe the XP toolset variants).  We eventually got around the
// problems without having to move the post-processing step to VAssistNet.dll.
//
// With the move to Sanctuary for licensing, the original purpose of the split
// has been left in the dust.

// license registration ui and network check handled in main dll
__interface IVaLicensingUi
{
	void LicenseInitFailed();
	BOOL DoTryBuy(int licenseStatus, bool nonPerpetualLicense);
	int ErrorBox(const char* msg, unsigned int options = MB_OK);
	void GetBuildDate(int& buildYr, int& buildMon, int& buildDay);
	void StartLicenseMonitor();
	void CheckForLicenseExpirationIfEnabled();
};

typedef void (*SplitUserLicenseInputFn)(const char* input, const char*& username, const char*& userkey);

// this interface can be implemented in a different dll than the main
// one, while the main one still hosts the UI. Whatever dll implements
// this interface must be processed by armadillo (and the dbgfixer).
__interface IVaLicensing
{
	enum RegisterResult
	{
		ErrShortInvalidKey,
		ErrRenewableLicenseExpired,
		ErrNonRenewableLicenseExpired,
		ErrNonPerpetualLicenseExpired,
		ErrIneligibleRenewal,
		ErrInvalidKey,
		OkTrial,
		OkLicenseAccepted,
	};

	enum LicenseCommands
	{
		cmdSubmitHelp = 1,
		cmdPurchase,
		cmdRenew,
		cmdCheckout,
		cmdCheckin
	};

	// armadillo environment queries
	int GetArmDaysInstalled();
	int GetArmDaysLeft();
	LPCSTR GetArmString(const LPCSTR str);
	int GetArmClock();
	int IsArmExpired();

	// armadillo license methods
	RegisterResult RegisterArmadillo(HWND hWnd, const LPCSTR user, const LPCSTR key, SplitUserLicenseInputFn splitter);

	// sanctuary
	BOOL IsSanctuaryLicenseInstalled(bool checkValid = false);

	// generic license methods
	BOOL InitLicense(IVaLicensingUi * vaLicUi);
	void Shutdown();
	int GetLicenseStatus();
	LPCSTR GetErrorMessage();
	int GetLicenseUserCount();
	LPCSTR GetLicenseExpirationDate();
	BOOL IsNonRenewableLicenseInstalled();
	BOOL IsNonPerpetualLicenseInstalled();
	BOOL IsAcademicLicenseInstalled();
	BOOL IsVaxNetworkLicenseCountSupported();
	BOOL IsVaxNetworkLicenseCountOk();
	void UpdateLicenseStatus();
	DWORD LicenseCommandQueryStatus(LicenseCommands cmd);
	void LicenseCommandExec(LicenseCommands cmd);
	LPCWSTR GetLicenseInfoUser(bool forceLoad = false);
	LPCSTR GetLicenseInfoKey(bool forceLoad = false);
};

extern IVaLicensing* gVaLicensingHost;
