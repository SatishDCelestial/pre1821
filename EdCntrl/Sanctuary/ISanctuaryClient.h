#pragma once

//
// this is the interface that VA uses to talk to Sanctuary
__interface ISanctuaryClient
{
	virtual bool checkStatus();
	virtual bool HasProductInfo() const;
	virtual bool IsConcurrentLicenseInstalled() const;
	virtual bool IsNamedNetworkLicenseInstalled() const;
	virtual bool IsSuperkeyLicenseInstalled() const;
	virtual bool IsLicenseValidForThisBuild(bool displayUi, HWND uiParent = nullptr);

	virtual bool IsRenewableLicense() const;
	virtual bool IsPersonalLicense() const;
	virtual bool IsAcademicLicense() const;
	virtual LPCSTR GetMaintenanceEndDate() const;
	virtual LPCWSTR GetPortalUserName() const;
	virtual LPCSTR GetSerialNumber() const;

	virtual int GetRegistrationCode();
	virtual bool registerSerial(const LPCWSTR serialNumber, const LPCWSTR userName, const LPCWSTR password);
	virtual bool ImportLicenseFile(const LPCWSTR file, LPCSTR *errorInfo);
	virtual void reload();
	virtual LPCSTR getLastErrorMsg();

	virtual bool SupportsOfflineCheckout();
	virtual bool HasOfflineCheckout();
	virtual int GetOfflineHoursLeft();
	virtual int GetOfflineHoursGranted();
	virtual int OfflineCheckout(int hours);
	virtual int OfflineCheckin();
};

using SanctuaryClientPtr = ISanctuaryClient *;
