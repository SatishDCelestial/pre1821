//---------------------------------------------------------------------------
// Copyright (c) 2003-04 Borland Software Corporation.  All Rights Reserved.
//---------------------------------------------------------------------------

#pragma once

#if defined(SANCTUARY)

#include <WbemCli.h>
#include <memory>

#define _VIS_STD
#define _VIS_COMPAT

#include "Sanctuary/include/sanctuary.h"
#pragma warning(push, 1)
#include "Sanctuary/include/SlipManager.h"
#pragma warning(pop)
#pragma warning(disable: 4265)
#include "Sanctuary/include/nlic/ElHandler.h"
#include "ISanctuaryClient.h"

SANCT_USING_NAMESPACE

#define BUFSIZE MAX_PATH


/**
*This sample client was designed to run on Windows only.  To fully run client you must have the following pieces:
*
*  - SanctuaryClient.exe - this executable
*  - LicenseReg.exe      - License Registration Executable
*  - sanctuarylib.dll    - sanctuary dll, used at runtime by license registration executable.
*  - cglm.ini            - ini file used to initialize parameters for LicenseReg.exe.  You can also use cglm.ini
*                          to load parameters below rather then statically setting them.
**/

//------------------------ Explanation of SKU / SKU ID / Product ID ------------------------//
/*
	SKUs are sellable items from price list. 
	RAD Studio for example has hundreds of SKUs. 
	They differ in price, license type, product edition and other attributes. 
	SKUs are not included in product code.

	SKU ID in Sanctuary terms defines product edition (e.g. Professional, Enterprise, Architect...).
	That is optional identifier and some products do not have SKU ID hard coded. 
	Those products either have a single edition or include logic that takes that attribute from license and sets behavior based on that.

	ProductID is the most important identifier that has to be included in product code. 
	It identifies product family and version. 
**/

/**
  * Info from Vladan:
  * 
  * [2018-12-18 re: automated install]
  * Workstation licenses are node-locked and cannot be automatically generated via scripting. 
  *	There are three alternatives for this case and all three can be automated/scripted.
  *	- license server: One unlocked license is applied to all installs
  *	- eSlip: licenses that do not require user credentials, require one time activation and long heartbeat (e.g. 30 days) and cannot be over-deployed
  *	- superkey: unlocked license that can be over-deployed, however usage reporting is available
  *		
  *	Although it is technically possible to share a single EDN login and single key, in general that practice 
  *	is avoided because it is impossible to control who is actually using the product both by customer and us. 
  *	A better alternative is a single, multi-user key and individual logins.
  *	
  * [2019-01-23]
  * You can get SKU ID from a license if you want to know if user purchased Standard or Personal.
  * We do have a separate flag for Academic, so you can check that as well.
  * 
  * [2019-01-24]
  * Types of licenses:
  *		- workstation (machine locked)
  *		- network license (unlocked, controlled by license server)
  *		- eSlip (unlocked, controlled by sanctuary server) 
  *		- superkey (unlocked, server side disabling option, usage reporting available)
  *	Types of locks for workstation licenses:
  *		- SlipManager::LOCK_USER
  *		- SlipManager::LOCK_USER_NODE
  *		- SlipManager::LOCK_NODE_HOST
  *	Table of standard attributes available sent same day (see also ParamData.h).
  *	Subscription expiration date will be in the license as custom attribute; 
  *		key is sub.expdate (convert it from Java milliseconds to whatever format you need)
  *		
  *	[2019-02-01 re: multi-user licenses]
  *	1. Concurrent license with ELC: no end user info, strictly enforced limit (customer can always add more licenses)
  *	2. eSlip: one end user email, no license server, requires internet connection, strictly enforced limit (customer can always add more licenses)
  *	3. Superkey: one end user email, no license server, requires internet connection for initialization, no limit enforcement
  *	
  *	[2019-02-05 serial number -> account association]
  *	Retail serial number is associated with user account during registration in product.
  *	Trial serial number is associated with user account right after creation on website (though we aren't using Sanctuary trials).
  *	
  *	[2019-02-05 eslip info]
  *	eSlip is special unlocked license that does not activate product per se. 
  *	Once product loads it, it sends request to the main Sanctuary server which checks user limit and 
  *		returns actual locked license that is by default 30 day license. 
  *	User does not need internet connection between slip re-issuances, but internet connection is 
  *		required to get the initial license and to renew license. 
  *	30 days is default period, it is server side setting.
  *	
  *	[2019-02-12 custom attributes]
  *	custom attributes set by client are temporary.
  *	
  *	[2019-02-13 registration limits]
  *	serial numbers can only be registered to a single portal user account
  *	serial numbers can be registered on 3 machines
  *	those values are defined per package (VA has 3 packages: standard, academic, personal)
  *	there is no runtime accounting of serial numbers
  *	eSlips are not created for individual users.
  *		A single eSlip has predefined machine limit. 
  *		One eSlip is issued per order and order is typically for tens or hundreds of machines. 
  *		eSlip heartbeat is configurable, default is 30 days.
  *	
  *	[2019-03-14 license files]
  *	A license file is distributed either in a binary format (*.slip) or text format (reg*.txt). 
  *	Text format was introduced because binary files as email attachments are usually stripped off of emails.
  *	License file in either binary or text format can be locked (to a machine or machine+user) or unlocked. 
  *	A locked license is result of registration and can be applied only to a single machine or user+machine, depending on the locking mechanism. 
  *	VA uses machine+user locking. 
  *	An unlocked license can be applied to any machine or machine+user.
  *	VA can load locked and unlocked licenses in either binary or text format.
  *	A special type of unlocked license is eSlip. 
  *	eSlips can be distributed in either binary or text format. 
  *	eSlip itself does not activate product, it rather triggers connection to the main license server which checks current user count and if seats are available, sends back locked license (*.slip) that activates the product.
  *	
  */

  /**
	* Info from Kenichi:
	* 
	* [2019-02-12 re: servers]
	* Debug version of Sanctuary lib communicates with license-stage.embarcadero.com
	* Release version of Sanctuary lib communicates with license.embarcadero.com
	* To use production server in debug build, add following line to 
	* C:\Windows\System32\drivers\etc\hosts :
		204.61.221.88  license-stage.embarcadero.com # license.embarcadero.com
	* The servers have independent licensing data (serial numbers, account logins and passwords).
	*/

//------------------------DEFAULT INITIALIZATION SECTION-----------------------------------//

/**
* Default product id variable.  Overridden by product id in cglm.ini file.
*
* productId is defined by Sanctuary and identifies a product and version
* eg. 1014 is JBuilder 2008
*
*/
static const int VisualAssistProductId = 2201; // per email from Vladan 2019-01-19


/**
* Default sku id variable.  Overridden by sku id in cglm.ini file.
*
* skuId is is defined by Sanctuary and identifies a sku or edition
* eg. 1 = Enterprise, 2 = Professional, 3 = Personal
* skuId values are product specific
*
*/
static const int StandardSku = 16;
static const int PersonalSku = 8; // per email from Vladan 2019-01-31


/**
* Default locking type variable.  Overridden by locking type in cglm.ini file.
*
* set license locking mechanism. All user-locked products store licenses
* in the shared license storage USER_HOME/.codegear/.cg_license
* Each node locked product stores its license in the .cg_license file, located in
* the infoDir directory.
*
* Available locktypes are:
* - SlipManager::LOCK_NODE_HOST   - sanctuary generates lock using encrypted hostname.
* - SlipManager::LOCK_NODE_CUSTOM - API user passes custom lock to sanctuary.
* - SlipManager::LOCK_USER        - sanctuary generates lock using userhome directory and username.
*
*/
static const int def_lockType   = SlipManager::LOCK_USER;


/**
* Default info directory.  Overridden by setting in cglm.ini file if setting
* exists.
*
* infoDir is directory where license storage (.cg_license file) is located.
* This directory can be NULL, in which case the following rules apply:
*
* - user locked products, infoDir defaults to <USER_HOME>/.codegear.
* - node locked products, infoDir defaults to directory pointed to by
*   licenseDir .
*
* For systems such as VISTA, you may need to explicitly set the infoDir
* to store licensing information to All Users/AppData/Embarcadero/.licenses
* directory.
*
* User MUST have read-write capability in this directory.
*
*/
static const wchar_t* def_infoDir = L"";


/**
* Default Slip Loading Directory.  Overridden by setting in cglm.ini file if
* setting exists.
*
* Directory for loading auto-generated slip files (aka license files).
* This directory can be NULL in which case the following rules apply:
*
*  - user locked products, slipDir defaults to <USER_HOME>.
*  - node locked products, slipDir defaults to licenseDir.
*
* For systems such as VISTA, you may need to explicitly set the slipDir
* to store licensing information to All Users/AppData/Embarcadero directory.
*
* User MUST have read-write capability in this directory.
*
*/
static const wchar_t* def_slipDir    = L"";

/**
*
*  This is the license manager GUI executable.  You probably will not
*  need to call this from any product directly, however products like
*  RAD Studio install this into their bin directory and create a shortcut
*  on the start menu.
*
**/
static const wchar_t* licenseManagerExe = L"LicenseManager.exe";


//-------------CGLM INI KEYS SECTION DO NOT CHANGE------------------------
static const wchar_t* CODEGEAR_INI_FILE    = L"cglm.ini";
static const wchar_t* CODEGEAR_APP_SECTION = L"Embarcadero License Management";
static const wchar_t* ROOT_DIR_KEY         = L"RootDir";
static const wchar_t* LOCK_TYPE_KEY        = L"LockType";
// static const char* PRODUCT_KEY          = "ProductId";
// static const char* SKU_KEY              = "SkuId";
static const wchar_t* REG_EXE_KEY          = L"RegExe";
static const wchar_t* INFO_DIR_KEY         = L"InfoDir";
static const wchar_t* SLIP_DIR_KEY         = L"SlipDir";
static const wchar_t* LICENSE_DIR_KEY      = L"LicenseDir";
//--------------------END SECTION-----------------------------------------

// LicenseHandler

class LicenseHandler : public ElHandler
{
public:
	LicenseHandler(SlipManager& manager) : ElHandler(manager)
	{
	}

	int handleError(SanctLicense& license, const SanctHeartbeat& heartbeat);

public:
	int handleEvent(int event, SanctLicense& license, const SanctHeartbeat& heartbeat, int rc);
};

#if !defined(SANCT_NO_RENEWAL)
class SanctLicenseRenewalCallback;
class SanctLicenseRenewalPrefs;
#endif // !defined(SANCT_NO_RENEWAL)

#if !defined(SANCT_NO_SUBSCRIPTION)
SANCT_BEGIN_NAMESPACE
class LicenseVerificationCallback;
SANCT_END_NAMESPACE
#endif // !defined(SANCT_NO_SUBSCRIPTION)

// SanctuaryClient

class SanctuaryClient : public ISanctuaryClient
{
private:
	static VISMutex s_mtxInstanceCount; // simple
	static int s_instanceCount;

	SlipManager* manager;
	OrderedProductInfo* info;
	LicenseHandler* handler;
	SanctLicense* license;

#if !defined(SANCT_NO_RENEWAL)
	SanctLicenseRenewalCallback* renewalCallback;
	SanctLicenseRenewalPrefs* renewalPrefs;
#endif // !defined(SANCT_NO_RENEWAL)

#if !defined(SANCT_NO_SUBSCRIPTION)
	LicenseVerificationCallback* verificationCallback;
#endif // !defined(SANCT_NO_SUBSCRIPTION)

#if !defined(SANCT_USE_REGEXE)
	RegistrationStatus lastError;
#endif // !defined(SANCT_USE_REGEXE)

	String coUserName;
	String coHostName;

	bool checked;
	bool licensed;
	bool checkedOut = false;
	int licServerType;
	int licType;

	int productId;
	int lockType;
	String rootDir;
	String licenseDir;
	String infoDir;
	String slipDir;

public:
	SanctuaryClient();
	~SanctuaryClient();

	void initSanctuary();
	// Note: if we start to use Sanctuary for trial licenses, every call to checkStatus 
	// will have to be updated because return true has meant not temporary license.
	bool checkStatus() override;
	bool HasProductInfo() const override;
	bool IsConcurrentLicenseInstalled() const override;
	bool IsNamedNetworkLicenseInstalled() const override;
	bool IsSuperkeyLicenseInstalled() const override;
	bool ImportLicenseFile(const LPCWSTR file, LPCSTR *errorInfo) override;
	int GetRegistrationCode() override;
	bool IsLicenseValidForThisBuild(bool displayUi, HWND uiParent = nullptr) override;
	LPCSTR GetSerialNumber() const override;
	LPCWSTR GetPortalUserName() const override;
	LPCSTR GetMaintenanceEndDate() const override;
	void GetMaintenanceEndDate(int& expYr, int& expMon, int& expDay) const;
	void GetMaintenanceEndDate(const String *attr, int& expYr, int& expMon, int& expDay) const;
	bool IsPersonalLicense() const override;
	bool IsAcademicLicense() const override;
	bool IsRenewableLicense() const override;
	bool SupportsOfflineCheckout() override;
	bool HasOfflineCheckout() override;
	int GetOfflineHoursLeft() override;
	int GetOfflineHoursGranted() override;
	int OfflineCheckout(int hours) override;
	int OfflineCheckin() override;

	void SetCoUserName(const char* userName);
	void SetCoHostName(const char* hostName);
	void ReleaseDeployment(const String& coUserName, const String& coHostName);
	bool usingLicenseServer();
	String getLicServerName();
	int getNetworkLicenseType();
	bool usingBelise();
	void getUserName(String& userName);

	// perform online license check-in
	int checkin(); 
	// perform offline license check-in with duration=0, perform offline license check-out with duration (unit is Milli-Second)
	// return code of 0 does not mean success.
	int borrow(SanctLong duration);
	// returns remaining amount of borrowed time
	SanctLong getBorrowingLeft();

	SanctLong getBorrowingLimit();
	virtual void reload() override;
#if 0
	void report(bool showAll);
#endif
	void cleanupAndExit();
	const String* getCustomAttribute(const String& key) const;
	void setCustomAttribute(const String& key, const String& value) const;
	const String* getStandardAttribute(const String& key) const;

#if !defined(SANCT_USE_REGEXE)
	virtual bool registerSerial(const LPCWSTR serialNumber, const LPCWSTR userName, const LPCWSTR password) override;
	virtual LPCSTR getLastErrorMsg() override;
#endif // !defined(SANCT_USE_REGEXE)

	static void handleLicensingError(const sanct_error* s_error, SanctLong remainingTime, bool isBorrowing, SanctLicense* license);

#if 0
	static void PrintAttributes(const OrderedProductInfo* info, bool standard);
#endif

private:
	void loadIni();
	void check();
	void checkProductInfo();
	OrderedProductInfo* GetBestProductInfo();
	void CheckNetworked();
	void CheckDeployment(const String& coUserName, const String& coHostName);
	int UpdateDeployment(SanctLong duration, const String& coUserName, const String& coHostName);
	int checkout(); // perform online license check-out
	void ClearLastError();

	static void GetDetailErrorMessage(String* errorMsg, const sanct_error* s_error, const SanctLicense* license);
};


#pragma warning(default: 4265)

#endif
