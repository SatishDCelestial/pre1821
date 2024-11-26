#include "stdafx.h"
#pragma warning(disable: 5219 5246)
#include "utils3264.h"

#if defined(SANCTUARY)

#include "SanctuaryClient.h"

#include "Sanctuary/include/SlipManagerFactory.h"
#include "Sanctuary/include/nlic/LicenseFactory.h"
#include "Sanctuary/include/PathPlaceholderHelper.h"

#if !defined(SANCT_NO_RENEWAL)
#include "SanctLicenseRenewalCallback.h"
#include "SanctLicenseRenewalPrefs.h"
#endif // !defined(SANCT_NO_RENEWAL)

#if !defined(SANCT_NO_SUBSCRIPTION)
#include "Sanctuary/include/verification/ILicenseVerificationCallback.h"
// sample logging implementation, no need to include when you have own one
#include "Sanctuary/include/verification/LicenseVerificationCallback.h"
#endif // !defined(SANCT_NO_SUBSCRIPTION)

#include "Sanctuary/include/MytoolsDescriptor.h"

#include "SanctStrerror.h"
#include <chrono>
#include "Addin/BuyTryDlg.h"
#include "Addin/Register_Strings.h"
#include "RegKeys.h"
#include "IVaAccessFromAux.h"

//#ifdef _DEBUG
//#pragma comment(lib, "../../../3rdParty/Sanctuary/lib/x86/Debug/sanctuarylib.lib")
//#pragma comment(lib, "../../../3rdParty/Sanctuary/x86/openssl-1.0.2j/lib/libeay32MTd.lib")
//#pragma comment(lib, "../../../3rdParty/Sanctuary/x86/openssl-1.0.2j/lib/ssleay32MTd.lib")
//#else
//#pragma comment(lib, "../../../3rdParty/Sanctuary/lib/x86/Release/sanctuarylib.lib")
//#pragma comment(lib, "../../../3rdParty/Sanctuary/x86/openssl-1.0.2j/lib/libeay32MT.lib")
//#pragma comment(lib, "../../../3rdParty/Sanctuary/x86/openssl-1.0.2j/lib/ssleay32MT.lib")
//#endif



//initialization sets the license subdirectory to rootDir + "license"
static const wchar_t* LICENSE_SUBDIRECTORY = L"license";
// name of custom attribute for licensing portal username or email
static const char* PORTAL_USERNAME = "PortalUser";
static const char* ExpirationDateAttributeName = "sub.expdate";


//only used for locking down static instance count
VISMutex SanctuaryClient::s_mtxInstanceCount;
int SanctuaryClient::s_instanceCount = 0;

//Sanctuary will set correct username/hostname
//if license server is in use.  These are only default settings.
static const char* defaultCoUserName = "my-user";
static const char* defaultCoHostName = "my-host";

extern IVaAccessFromAux* gVaInfo;

#define LogToVa(s)			{ if (gVaInfo) gVaInfo->LogStr(s); }

String
WideToMbcs(LPCWSTR bstrText, int len, int mbcsCodepage)
{
	if (len)
	{
		// lossy conversion if chars in text aren't able to be mapped to mbcsCodepage
		int blen = WideCharToMultiByte((UINT)mbcsCodepage, 0, bstrText, len, nullptr, 0, nullptr, nullptr);
		char * cbuf = new char[(size_t)blen + 2];
		if (cbuf)
		{
			/*int l =*/ WideCharToMultiByte((UINT)mbcsCodepage, 0, bstrText, len, cbuf, blen, nullptr, nullptr);
			cbuf[blen] = '\0';
			String rstr(cbuf);
			delete[] cbuf;
			return rstr;
		}
	}

	return String();
}

String
WideToSanctString(LPCWSTR wtstr)
{
	const CStringW wideStr(wtstr);
	// convert to MBCS using CP_ACP for sanctuary (instead of passing utf16)
	return ::WideToMbcs(wideStr, wideStr.GetLength(), CP_ACP);
}

bool
WtIsFile(const CStringW & file)
{
	if (file.IsEmpty())
		return false;

	// confirm that it is a file and not a directory
	DWORD attr = GetFileAttributesW(file);
	if (INVALID_FILE_ATTRIBUTES == attr || (attr & FILE_ATTRIBUTE_DIRECTORY))
		return false;	//	not a file

	return true;
}

int
WtMessageBox(HWND hWnd, LPCSTR lpText, const LPCSTR lpCaption, UINT uType /*= MB_OK*/)
{
	CString logMsg;
	CString__FormatA(logMsg, "WtMsgBox: %s\n", (const char *)CString(lpText));
	LogToVa(logMsg);
	return ::MessageBoxA(hWnd, lpText, lpCaption, uType);
}

LONG
WtSetRegValueW(HKEY rootKey, LPCSTR lpSubKey, LPCSTR name, const LPCWSTR valueIn)
{
	CStringW value(valueIn);
	DWORD dataSize = (value.GetLength() + 1) * sizeof(WCHAR);
	HKEY hKey;
	int idx;

	LSTATUS err = RegCreateKeyExW(rootKey, CStringW(lpSubKey), 0, (LPWSTR)L"", REG_OPTION_NON_VOLATILE,
		KEY_QUERY_VALUE | KEY_WRITE, nullptr, &hKey, (DWORD*)&idx);
	if (ERROR_SUCCESS == err)
		err = RegSetValueExW(hKey, CStringW(name), 0, REG_SZ, (LPBYTE)(LPCWSTR)value, dataSize);
	RegCloseKey(hKey);
	return (LONG)err;
}

CStringW
WtGetRegValueW(HKEY rootKey, const char* key, const char* name, LPCWSTR defaultValue)
{
	CStringW retval(defaultValue);
	HKEY	hKey;
	LSTATUS err = RegOpenKeyExW(rootKey, CStringW(key), 0, KEY_QUERY_VALUE, &hKey);
	if (ERROR_SUCCESS == err)
	{
		DWORD 	dataSize = 0, dataType = 0;
		// get string length first
		err = RegQueryValueExW(hKey, CStringW(name), nullptr, &dataType, nullptr, &dataSize);
		if (ERROR_SUCCESS == err && dataSize)
		{
			_ASSERTE(REG_SZ == dataType);
			// allocate a buffer
			dataSize += sizeof(WCHAR);
			const std::unique_ptr<WCHAR[]> strDataVec(new WCHAR[dataSize + 1]);
			LPWSTR strData = &strDataVec[0];
			ZeroMemory(strData, (dataSize + 1) * sizeof(WCHAR));

			// get the value
			err = RegQueryValueExW(hKey, CStringW(name), nullptr, &dataType, (LPBYTE)strData, &dataSize);
			if (ERROR_SUCCESS == err)
			{
				// added this check to prevent returning empty strings with a 
				// length of 1 - just return an empty CString instead
				if (!(dataSize == 1 && !strData[0]))
					retval = strData;
			}
		}
		RegCloseKey(hKey);
	}
	return retval;
}

static bool
DoesMaintenanceSupportDate(WORD expYear, WORD expMonth, WORD expDay, WORD chkYear, WORD chkMonth, WORD chkDay)
{
	_ASSERTE(chkYear && chkMonth && chkDay);
	if (chkYear && chkMonth && chkDay)
	{
		_ASSERTE(expYear && expMonth && expDay);
		if (expYear > chkYear)
			return true;
		if (expYear == chkYear)
		{
			if (expMonth > chkMonth)
				return true;
			if (expMonth == chkMonth)
			{
				if (expDay >= chkDay)
					return true;
			}
		}
	}
	return false;
}

static void
MilliSecToSystemTime(UINT64 msec, SYSTEMTIME *st)
{
	ULARGE_INTEGER ns;
	ns.QuadPart = 10000 * msec; // convert to nanoseconds

	FILETIME ft;
	ft.dwHighDateTime = ns.HighPart;
	ft.dwLowDateTime = ns.LowPart;

	::FileTimeToSystemTime(&ft, st);
}

static void
GetDateFromMilliSec(UINT64 msec, int &yr, int &mo, int &day)
{
	SYSTEMTIME st;
	MilliSecToSystemTime(msec, &st);

	// FILETIME starts at Jan 1, 1601, but java msec starts at Jan 1, 1970
	st.wYear += (1970 - 1601);

	yr = st.wYear;
	mo = st.wMonth;
	day = st.wDay;
}


/**
*  Constructor for starting the Sanctuary Client.
*  This client ONLY supports one instance per process.
*
*  @throws RuntimeException if invoked multiple times.
**/
SanctuaryClient::SanctuaryClient() :
	coUserName(defaultCoUserName),
	coHostName(defaultCoHostName),
	productId(0),
	lockType(-1)
{
	VISMutex_var lock(s_mtxInstanceCount); // simple

	if (s_instanceCount > 0)
	{
		throw RuntimeException("\n---This sample supports running one instance of Sanctuary at a time.---\n");
	}
	++s_instanceCount;

	manager = nullptr;
	info = nullptr;
	license = nullptr;
	handler = nullptr;

#if !defined(SANCT_NO_RENEWAL)
	renewalCallback = nullptr;
	renewalPrefs = nullptr;
#endif // !defined(SANCT_NO_RENEWAL)

#if !defined(SANCT_NO_SUBSCRIPTION)
	verificationCallback = nullptr;
#endif // !defined(SANCT_NO_SUBSCRIPTION)

	licServerType = ParamData::SERVER_NONE;
	licType = ParamData::LICTYPE_NONE;

	licensed = false;
	checked = false;
}

SanctuaryClient::~SanctuaryClient()
{
	VISMutex_var lock(s_mtxInstanceCount); // simple
	--s_instanceCount;

	if (checkedOut)
	{
		// [case: 142143]
		_ASSERTE(license);
		checkin();
	}

	if (manager != nullptr)
		SlipManagerFactory::ReleaseSlipManager(manager);

	if (license != nullptr)
	{
		// in the original SanctuaryClient code, the license is checked in at this point
//		license->checkin();
		delete license;
	}

	if (handler != nullptr)
		delete handler;

#if !defined(SANCT_NO_RENEWAL)
	if (renewalCallback != nullptr)
		delete renewalCallback;

	if (renewalPrefs != nullptr)
		delete renewalPrefs;
#endif // !defined(SANCT_NO_RENEWAL)

#if !defined(SANCT_NO_SUBSCRIPTION)
	if (verificationCallback != nullptr)
		delete verificationCallback;
#endif // !defined(SANCT_NO_SUBSCRIPTION)
}

/**
* Sanctuary license manager initialization method.
*/
void
SanctuaryClient::initSanctuary()
{
	checked = false;
	licensed = false;

	//loads settings from local cglm.ini file
	LogToVa("sc: b lini\n");
	loadIni();
	LogToVa("sc: c lini\n");

	//set our individual product's productId AND sku here.  this is not the suite productId or sku.
	productId = VisualAssistProductId;

	if (lockType == SlipManager::LOCK_NODE_CUSTOM)
	{
		//Get our own node lock from built in library.
		//Constructor which takes custom node lock when initializing sanctuary.
		//This is the constructor to use if you want to use your own custom lock.
		_ASSERTE(!"sanctuary LOCK_NODE_CUSTOM not enabled");
// 		char* id = new char[50];
// 		node_get_id(id);
// 		manager = SlipManagerFactory::CreateSlipManager(infoDir.c_str(), rootDir.c_str(), licenseDir.c_str(), slipDir.c_str(), productId, id);
	}
	else
	{
		//Constructor which passes lock type and then lets sanctuary decide what the lock is.
		//Instead of using constructor, we use the appropriate method from SlipManagerFactory
		LogToVa("sc: b csm\n");
		manager = SlipManagerFactory::CreateSlipManager(infoDir, rootDir, licenseDir, slipDir, productId, lockType);
		LogToVa("sc: f csm\n");
	}

	try
	{
#if !defined(SANCT_NO_RENEWAL)
		// enable silent activation before calling SlipManager::load()
		if (renewalCallback == nullptr && renewalPrefs == nullptr)
		{
			renewalCallback = new SanctLicenseRenewalCallback();
// #if (0)
// 			renewalPrefs = new SanctLicenseRenewalPrefs();
// #else // (0)
// 			renewalPrefs = new SanctLicenseRenewalPrefs(sku);
// #endif // (0)
			LogToVa("sc: b csa\n");
			manager->configSilentActivation(true, renewalCallback/*, renewalPrefs*/);
			LogToVa("sc: f csa\n");
		}
#endif // !defined(SANCT_NO_RENEWAL)
		//reads license storage file and loads slips from slip dir and license dir.
		LogToVa("sc: b mgrLoad\n");
		// [case: 137881] this call is known to hang the library if user has a large number of .txt files in their home dir
		// [case: 141482] this call is known to hang the library if user has a single very large .txt file in their home dir
		manager->load();
		LogToVa("sc: f mgrLoad\n");
	}
	catch (const SlipException& ex)
	{
		String error;
		SanctuaryExt::GetErrorMessage(&error, ex);
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::init 1 %s\n", error.getChars());
		LogToVa(logMsg);
	}
	catch (...)
	{
		LogToVa("ERROR: sclnt::init 2\n");
	}
}

void
SanctuaryClient::loadIni()
{
	wchar_t Buffer[BUFSIZE];

	if (!gVaInfo)
		return;

	try
	{
		const CStringW userDir(gVaInfo->GetUserDir());
		const CStringW lpFileName(userDir + CODEGEAR_INI_FILE);
		if (::WtIsFile(lpFileName))
		{
			LogToVa("sclnt::loadini\n");
		}

		LPCWSTR lpAppName = CODEGEAR_APP_SECTION;
		//set ROOT_DIR from cglm.ini file

		/* In order to support On-Demand applications (which have no installer
		* to set up an absolute rootDir property in cglm.ini), products should
		* disregard rootDir property in cglm.ini and use some other way to
		* compute the application root directory (- GetModuleFileName).
		* RootDir property is now reserved for use by LicenseReg and LicenseManager
		* which have no other way of knowing where in the application directory
		* they reside.
		*/
		CStringW tmp = userDir;
		rootDir = WideToSanctString(tmp);
		Buffer[0] = L'\0';

		//set SLIP_DIR from cglm.ini file
		GetPrivateProfileStringW(
			lpAppName,
			LICENSE_DIR_KEY,
			userDir + LICENSE_SUBDIRECTORY,
			Buffer,
			BUFSIZE,
			lpFileName
		);

		tmp = Buffer;
		licenseDir = ::WideToSanctString(tmp);
		Buffer[0] = L'\0';

		// Paths in cglm.ini may contain placeholders such as "${ROOTDIR}",
		// "${HOME}", "${APPDATA}". Expand those here to log them, although
		// paths could be passed unexpanded to SlipManager constructor.
		PathPlaceholderHelper helper(rootDir);
		licenseDir = helper.ExpandPlaceholders(licenseDir).getChars();

		//set LOCK TYPE from cglm.ini file
		lockType = (int)GetPrivateProfileIntW(
			lpAppName,
			LOCK_TYPE_KEY,
			def_lockType,
			lpFileName
		);

		//set INFO_DIR from cglm.ini file
		GetPrivateProfileStringW(
			lpAppName,
			INFO_DIR_KEY,
			def_infoDir,
			Buffer,
			BUFSIZE,
			lpFileName
		);

		tmp = Buffer;
		infoDir = ::WideToSanctString(tmp);
		Buffer[0] = L'\0';

		if (!infoDir.length())
		{
			//PROVIDES AN EXAMPLE FOR OBTAINING THE GENERAL INFODIR -- location dependent upon lockType
			// For user lock, is Embarcadero\.licenses dir in user roaming dir.
			File embacaderoLicenseDir;
			LicenseFileHelper::FindHelperDir(&embacaderoLicenseDir, lockType, LicenseFileHelper::INFO_DIR_TYPE);
			if (embacaderoLicenseDir.Exists())
				infoDir = embacaderoLicenseDir.GetPath();
		}

		//set SLIP_DIR from cglm.ini file
		GetPrivateProfileStringW(
			lpAppName,
			SLIP_DIR_KEY,
			def_slipDir,
			Buffer,
			BUFSIZE,
			lpFileName
		);

		tmp = Buffer;
		slipDir = ::WideToSanctString(tmp);
		Buffer[0] = L'\0';

		if (!slipDir.length())
		{
			//PROVIDES AN EXAMPLE FOR OBTAINING THE GENERAL SLIPDIR -- location dependent upon lockType
			// For user lock, is Embarcadero dir in user roaming dir.
			File embacaderoDir;
			LicenseFileHelper::FindHelperDir(&embacaderoDir, lockType, LicenseFileHelper::SLIP_IMPORT_DIR_TYPE);
			if (embacaderoDir.Exists())
				slipDir = embacaderoDir.GetPath();
		}
	}
	catch (RuntimeException& ex)
	{
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::loadini %s\n", ex.what());
		LogToVa(logMsg);
	}
}


/**
* This method needs to be called from a hosting product.  Used to determine
* whether a license is active and available or not.  Will launch LicenseReg.exe
* if necessary.
*/
bool
SanctuaryClient::checkStatus()
{
	check();
	return licensed;
}

bool
SanctuaryClient::ImportLicenseFile(const LPCWSTR file, LPCSTR *errorInfo)
{
	static String importErrorInfo;
	if (!manager)
		return false;

	const String licenseFilename(::WideToSanctString(file));
	ClearLastError();

	try
	{
		// load a license and copy the file into specific folder for next loading
		LogToVa("sc: b ilf\n");
		manager->importSlip(licenseFilename, true);
		LogToVa("sc: f ilf\n");
		checked = false;
	}
	catch (SlipException& ex)
	{
		SanctuaryExt::GetErrorMessage(&importErrorInfo, ex);
		lastError.msg = importErrorInfo;
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::ilf 1 %s\n", importErrorInfo.getChars());
		LogToVa(logMsg);
		if (errorInfo)
			*errorInfo = importErrorInfo.getChars();
		return false;
	}
	catch (RuntimeException& ex)
	{
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::ilf 2 %s\n", ex.what());
		LogToVa(logMsg);
		importErrorInfo = ex.what();
		lastError.msg = importErrorInfo;
		if (errorInfo)
			*errorInfo = importErrorInfo.getChars();
		return false;
	}

	return true;
}

int
SanctuaryClient::GetRegistrationCode()
{
	if (!manager)
		return 0;

	// per email from Vladan 2019-02-05
	LogToVa("sc: sessionId\n");
	return manager->getSessionId();
}

bool
SanctuaryClient::IsLicenseValidForThisBuild(bool displayUi, HWND uiParent /*= nullptr*/)
{
	if (!checked)
		check();

	if (!licensed)
	{
		LogToVa("sc: ilvftb none\n");
		_ASSERTE(!displayUi);
		return false;
	}

	CString	msgMsg;
	int expYear, expMonth, expDay;
	GetMaintenanceEndDate(expYear, expMonth, expDay);
	if (!expYear || !expMonth || !expDay)
	{
		LogToVa("sc: ilvftb mtx date\n");
		if (displayUi)
		{
			msgMsg = "Product license error.\n\n"
				"Please report error SR-4 to\n"
				"http://www.wholetomato.com/contact";
			::WtMessageBox(uiParent, msgMsg, IDS_APPNAME, MB_OK | MB_ICONERROR);
		}
		return false;
	}

	int buildYear = 0, buildMon = 0, buildDay = 0;
	if (gVaInfo)
		gVaInfo->GetBuildDate(buildYear, buildMon, buildDay);
	if (!::DoesMaintenanceSupportDate((WORD)expYear, (WORD)expMonth, (WORD)expDay, (WORD)buildYear, (WORD)buildMon, (WORD)buildDay))
	{
		LogToVa("sc: ilvftb 0\n");
		if (displayUi)
		{
			if (IsRenewableLicense())
				msgMsg = kMsgBoxText_RenewableKeyExpired;
			else
				msgMsg = kMsgBoxText_NonrenewableKeyExpired;

			msgMsg.Replace("key", "serial number");

			::WtMessageBox(uiParent, msgMsg, kMsgBoxTitle_KeyExpired, MB_OK | MB_ICONERROR);
		}
		return false;
	}

	LogToVa("sc: ilvftb 1\n");
	return true;
}

LPCSTR
SanctuaryClient::GetSerialNumber() const
{
	static CString sn;
	sn.Empty();
	const String* attr = getStandardAttribute(ParamData::ATTR_SERIALNO);
	if (attr)
	{
		sn = attr->getChars();
		if (sn.GetLength() < 2)
			sn.Empty();
	}
	return sn;
}

LPCWSTR
SanctuaryClient::GetPortalUserName() const
{
	if (IsConcurrentLicenseInstalled() || IsNamedNetworkLicenseInstalled())
	{
		// portal name not used for NNU or floating/concurrent license
		return L"";
	}

	// portal name will only be valid for registrations that we handled
	// slip import will not have portal name

	static CStringW portalUsername;
	portalUsername = WtGetRegValueW(HKEY_CURRENT_USER, ID_RK_APP_KEY, PORTAL_USERNAME, nullptr);
	return portalUsername;
}

void
SanctuaryClient::GetMaintenanceEndDate(int& expYr, int& expMon, int& expDay) const
{
	const String *expDateInMillisecondsStr = getCustomAttribute(ExpirationDateAttributeName);
	GetMaintenanceEndDate(expDateInMillisecondsStr, expYr, expMon, expDay);
}

void
SanctuaryClient::GetMaintenanceEndDate(const String *expDateInMillisecondsStr, int& expYr, int& expMon, int& expDay) const
{
	expYr = expMon = expDay = 0;
	if (!expDateInMillisecondsStr || !expDateInMillisecondsStr->getChars())
		return;

	// https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/strtoui64-wcstoui64-strtoui64-l-wcstoui64-l?view=vs-2017
	UINT64 millisecs = ::_strtoui64(expDateInMillisecondsStr->getChars(), nullptr, 10);
	::GetDateFromMilliSec(millisecs, expYr, expMon, expDay);
}

LPCSTR
SanctuaryClient::GetMaintenanceEndDate() const
{
	const String *expDateInMillisecondsStr = getCustomAttribute(ExpirationDateAttributeName);
	int expYr, expMo, expDay;
	GetMaintenanceEndDate(expDateInMillisecondsStr, expYr, expMo, expDay);
	if (!expYr || !expMo || !expDay)
		return nullptr;

	static CString date;
	CString__FormatA(date, "%04d.%02d.%02d", expYr, expMo, expDay);
	return date;
}

bool
SanctuaryClient::IsPersonalLicense() const
{
	if (info)
	{
		int skuId = info->getSkuId();
		if (skuId == PersonalSku)
			return true;

		_ASSERTE(skuId == StandardSku);
	}

	return false;
}

bool
SanctuaryClient::IsAcademicLicense() const
{
	if (info)
	{
		const String* attr = getStandardAttribute(ParamData::ATTR_NONCOMMERCIAL);
		if (attr)
		{
			const char * s = attr->getChars();
			if (s)
			{
				if (*s == '1')
					return true;
			}
		}
	}

	return false;
}

bool
SanctuaryClient::IsRenewableLicense() const
{
	_ASSERTE(checked);
	if (!licensed && !HasProductInfo())
		return false;
	if (IsPersonalLicense())
		return false;
	if (IsAcademicLicense())
		return false;
	return true;
}

bool
SanctuaryClient::SupportsOfflineCheckout()
{
	if (IsAcademicLicense())
		return false;

	if (IsPersonalLicense())
		return false;

	if (!IsConcurrentLicenseInstalled())
		return false;

#if 0
	// not available in our sanct lib
	if (getAllowedDurationForRequestedUser())
		return true;

	return false;
#else
	return true;
#endif
}

bool
SanctuaryClient::HasOfflineCheckout()
{
	if (!SupportsOfflineCheckout())
	{
		LogToVa("sc: hoco-not\n");
		return false;
	}

	if (getBorrowingLeft() > 0)
		return true;
	return false;
}

int
SanctuaryClient::GetOfflineHoursLeft()
{
	if (!SupportsOfflineCheckout())
	{
		LogToVa("sc: gohl-not\n");
		return 0;
	}

	SanctLong left = getBorrowingLeft();
	if (left <= 0)
		return 0;

	int hours = (int)(left / 3600000.0);
	return hours;
}

int
SanctuaryClient::GetOfflineHoursGranted()
{
	if (!SupportsOfflineCheckout())
	{
		LogToVa("sc: gohg-not\n");
		return 0;
	}

	SanctLong granted = getBorrowingLimit();
	if (granted <= 0)
		return 0;

	int hours = (int)(granted / 3600000.0);
	return hours;
}

int
SanctuaryClient::OfflineCheckout(int hours)
{
	if (HasOfflineCheckout())
	{
		LogToVa("sc: oco-already\n");
		return SanctLicense::ERROR_INTERNAL;
	}

	double duration = hours * 60 * 60 * 1000;
	return borrow(duration);
}

int
SanctuaryClient::OfflineCheckin()
{
	if (!HasOfflineCheckout())
	{
		LogToVa("sc: oci-not\n");
		return SanctLicense::ERROR_INTERNAL;
	}

	return borrow(0);
}

/**
*  Retrieves the value of a custom attribute for the current license using the specified key.
*  Custom attributes can be created by Sanctuary, but can also be added
*  on the client side using OrderedProductInfo::setCustomAttribute method.
*
*  If there is no available current license or the key does not have a matching
*  entry in the custom attribute mapping, 0 will be returned.
*
*  For more information on accessing these values, see the PrintAttributes method in this class.
**/
const String* 
SanctuaryClient::getCustomAttribute(const String& key) const
{
	if (info)
	{
		LogToVa("sc: get_cav\n");
		return info->getCustomAttributeValue(key);
	}
	return nullptr;
}


/**
*  Sets/updates the value of an existing custom attribute for the current license
*  using the specified key.  If the key doesn't exist, then a new custom attribute
*  is created.
*
*  Custom attributes can be created by Sanctuary, but can also be added
*  on the client side using OrderedProductInfo::setCustomAttribute method.
*
*  If there is no available current license or the key does not have a matching
*  entry in the custom attribute mapping, 0 will be returned.
*
**/
void
SanctuaryClient::setCustomAttribute(const String& key, const String& value) const
{
	if (info)
	{
		info->setCustomAttribute(key, value);
	}
}


/**
*  Retrieves the value of a standard attribute for the current license using the specified key.
*  Standard attributes can only be set by Sanctuary.
*
*  If there is no available current license or the key does not have a matching
*  entry in the custom attribute mapping, 0 will be returned.
*
* For more information on accessing these values, see the PrintAttributes method in this class.
**/
const String*
SanctuaryClient::getStandardAttribute(const String& key) const
{
	if (info)
	{
		LogToVa("sc: get_av\n");
		return info->getAttributeValue(key);
	}
	return nullptr;
}

#if !defined(SANCT_USE_REGEXE)
/**
 * Registers user to a serial on the registration server.
 * Account of user must be created on the registration server prior registration.
 * Returns true when registration succeeds, otherwise returns false.
 * Last registration status will be hold in 'lastError'.
 **/
bool
SanctuaryClient::registerSerial(const LPCWSTR serialNumberIn, const LPCWSTR userNameIn, const LPCWSTR passwordIn)
{
	if (!manager)
		return false;

	try
	{
		// conversion in case username, etc require MBCS
		const String serialNumber(::WideToSanctString(serialNumberIn));
		const String userName(::WideToSanctString(userNameIn));
		const String password(::WideToSanctString(passwordIn));
		ClearLastError();
		LogToVa("sc: b regs\n");
		const bool res = manager->registerSerial(serialNumber, userName, password, lastError);
		if (res)
		{
			LogToVa("sc: f regs 1\n");
			WtSetRegValueW(HKEY_CURRENT_USER, ID_RK_APP_KEY, PORTAL_USERNAME, userNameIn);
		}
		else
			LogToVa("sc: f regs 0\n");
		return res;
	}
	catch (const RuntimeException &ex)
	{
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::rs %s\n", ex.what());
		LogToVa(logMsg);
		lastError.msg.append("\nRuntimeException: ");
		lastError.msg.append(ex.what());
	}

	return false;
}
#endif // !defined(SANCT_USE_REGEXE)

#if !defined(SANCT_USE_REGEXE)
/**
 * Returns last registration status message.
 **/
LPCSTR 
SanctuaryClient::getLastErrorMsg() 
{
	return lastError.msg.getChars();
}
#endif // !defined(SANCT_USE_REGEXE)


/**------------------------ Licensing Server API Methods -------------------------**/

/**
*  Checks whether a license server is being used based
*  upon the set license server type.
*
*  @return bool
**/
bool
SanctuaryClient::usingLicenseServer()
{
	return licServerType != ParamData::SERVER_NONE;
}

/**
* Retrieves the license server name in use.  Currently, Sanctuary supports two licensing
* servers, FlexLM and CodeGear Licensing Server.
*
* @return String
**/
String
SanctuaryClient::getLicServerName()
{
	return usingBelise() ? Data::BELISE : String();
}

int
SanctuaryClient::getNetworkLicenseType()
{
	if (info)
	{
		return info->getLicenseType();
	}
	return ParamData::LICTYPE_NONE;
}

/**
*  Checks whether the CodeGear License Server is in use.
*  Based upon the set license server type.
*
*  @return bool
**/
bool
SanctuaryClient::usingBelise()
{
	return licServerType == ParamData::SERVER_BELISE;
}

/**
* Checks out a license from the license server (FLEXLM or BELISE).
*
* IMPORTANT: Use when application starts even when borrowing a license.
* Connect to license server if possible.
* Start the heartbeat if connection attempt fails.
*
**/
int
SanctuaryClient::checkout()
{
	if (handler != nullptr)
	{
		delete handler;
		handler = nullptr;
	}
	handler = new LicenseHandler(*manager);
	if (license != nullptr)
	{
		delete license;
		license = nullptr;
	}
	int rc = SanctLicense::OK;
	LogToVa("sc: b co 1\n");
	license = LicenseFactory::CreateLicense(manager->getSessionId(), *info, *manager, *handler,
		info->getBorrowedMillisLeft() > 0, &rc);
	LogToVa("sc: f co 1\n");
	if (license == nullptr || rc != SanctLicense::OK)
	{
		CString msg;
		CString__FormatA(msg, "ERROR: sc: co 3 %p %d\n", license, rc);
		LogToVa(msg);

		if(license != NULL)
		{
			delete license;
			license = NULL;
		}
		return rc != SanctLicense::OK ? rc : SanctLicense::ERROR_INTERNAL;
	}
	else
	{
		LogToVa("sc: co 2\n");
		int res = license->checkout();
		if (SanctLicense::OK == res)
			checkedOut = true;
		return res;;
	}
}

/**
* Sets the username used when checking out a license from a license server.
* Allows API user to specify a different username other then the default
* when checking out the license.
**/
void
SanctuaryClient::SetCoUserName(const char* userName)
{
	coUserName = userName;
}

/**
* Sets the hostname used when checking out a license from a license server.
* Allows API user to specify a different hostname other then the default
* when checking out the license.
**/
void
SanctuaryClient::SetCoHostName(const char* hostName)
{
	coHostName = hostName;
}

/**
* Returns a license back to the pool of licenses on the licensing server.
*
* IMPORTANT: Use before application exits even when borrowing a license.
* Gracefully disconnect from license server.
* Note: Does *not* return a borrowed license to its free license pool.
*
*/
int
SanctuaryClient::checkin()
{
	if (license == nullptr)
	{
		return SanctLicense::ERROR_INTERNAL;
	}
	return license->checkin();
}

/**
*  Obtains the current username being used by the license server.
*  For Node locked licenses, this userName will be set to the
*  the name of the node using the license.
*/
void
SanctuaryClient::getUserName(String& userName)
{
	userName.setLength(0);
	if (license != nullptr)
	{
		userName = license->getCoUserName();
	}
}

/**
* Borrow a license in milliseconds.
* Once borrowed, borrow( 0 ) releases the license to its free license pool.
*
* @param duration
* @return int return code 0 indicates success.
* also returns code 0 on failure.
*/
int
SanctuaryClient::borrow(SanctLong duration)
{
	if (license == nullptr)
	{
		LogToVa("sc: error bor 1\n");
		return SanctLicense::ERROR_INTERNAL;
	}

	int res = license->borrow(duration);
	if (res)
	{
		CString logMsg;
		CString__FormatA(logMsg, "sc: error bor 2 0x%x\n", res);
		LogToVa(logMsg);
	}
	return res;
}

/**
 *
 * Returns the max amount of borrowed time (after it was requested).
 * Use this to confirm that successful call to borrow() got the duration requested.
 *
 * @return a value of 0 if borrowing is disabled or license is no longer borrowed.
 */
SanctLong
SanctuaryClient::getBorrowingLimit()
{
	SanctLong limit = 0;
	if (license != nullptr)
	{
		limit = license->getBorrowingLimit();
	}
	return limit;
}

/**
* Returns the remaining amount of borrowed time.
*
* @return a value of 0 if borrowing is disabled or expired
*/
SanctLong
SanctuaryClient::getBorrowingLeft()
{
	SanctLong remaining = 0;
	if (license != nullptr)
	{
		remaining = license->getBorrowingLeft();
	}
	return remaining;
}

/**
* Check and report license status for entered productId + sku
* If the license is temporary, missing or expired, registration wizard is launched
*/
void
SanctuaryClient::check()
{
	LogToVa("sc: chk\n");
	if (checked)
	{
		LogToVa("sc: chk rpt\n");
		return;
	}

	int daysLeft = INT_MAX;
	ClearLastError();
	checkProductInfo();
	checked = true;

	if (info == nullptr)
	{
		// missing license key
		LogToVa("sc: chk no\n");
	}
	else
	{
		if (usingLicenseServer())
		{
			// getLicServerName()

			int deployType = info->GetDeploymentType();
			switch (deployType)
			{
			case ParamData::DTYPE_NONE:
				LogToVa("sc: chk net\n");
				CheckNetworked();
				break;
			case ParamData::DTYPE_V1: {
				LogToVa("sc: chk dep\n");
				CheckDeployment(coUserName, coHostName);
				break;
			}
			default:
				licensed = false;
				lastError.msg = "Unsupported license deployment type";
				_ASSERTE(!"sanctuary: unsupported deployment type");
				LogToVa("ERROR: sclnt::chk\n");
			}
		}

		if (info->isTerm() || info->isTrial() || info->isEnterpriseInfoExactDate())
		{
			daysLeft = info->getDaysLeft();
			if (daysLeft <= 0)
			{
				// expired
				LogToVa("sc: chk exp\n");
			}
			else if (daysLeft == 1)
			{
				// expires tomorrow
				// #sanctuaryTodo decide if anything needs to happen here
				LogToVa("sc: chk exp 1\n");
			}
			else
			{
				// expires in daysLeft days
				// #sanctuaryTodo decide if anything needs to happen here
				CString logMsg;
				CString__FormatA(logMsg, "sc: chk exp x 0x%x\n", daysLeft);
				LogToVa(logMsg);
			}
		}
	}

	if (!usingLicenseServer() &&
		(!info || (info->isEnabled() && (info->isNaggy() || daysLeft <= 0))))
	{
		LogToVa("sc: chk err\n");
	}
}

void
SanctuaryClient::ClearLastError()
{
	lastError.code = lastError.index = 0;
	lastError.msg = "";
}

/**
*  Determines if the license specified by a productId and sku exist and is active
*  according to SlipManager::isActive method.
*/
void
SanctuaryClient::checkProductInfo()
{
	LogToVa("sc: cpi\n");
	info = GetBestProductInfo();
	if (info == nullptr)
	{
		LogToVa("sc: cpi none\n");
		licServerType = ParamData::SERVER_NONE;
		licType = ParamData::LICTYPE_NONE;
		licensed = false;
	}
	else
	{
		manager->finalizeProductInfo(info);
		licServerType = info->getLserverType();
		licType = info->getLicenseType();
		licensed = usingLicenseServer() || manager->isActive(info);
		if (licensed)
		{
			LogToVa("sc: cpi 1\n");
		}
		else
			LogToVa("sc: cpi 0\n");
	}
#if !defined(SANCT_NO_SUBSCRIPTION)
	if (manager->isSubscriptionAndMaintenanceMonitorRunning())
	{
		// subscription has been configured already
		LogToVa("sc: cpi run\n");
		return;
	}
	Vector v;
	manager->getSubscriptionAndMaintenanceProductInfoSet(v);
	if (!v.contains(info))
	{
		// current product is not in subscription
		LogToVa("sc: cpi no sub\n");
		return;
	}
	std::set<int> productIds;
	productIds.insert(productId);
	manager->configureSubscriptionAndMaintenanceMonitor(info);
	if (verificationCallback == nullptr)
	{
		verificationCallback = new LicenseVerificationCallback();
	}
	manager->startSubscriptionAndMaintenanceMonitor(productIds, verificationCallback, verificationCallback);
#endif // !defined(SANCT_NO_SUBSCRIPTION)
}

OrderedProductInfo*
SanctuaryClient::GetBestProductInfo()
{
	LogToVa("sc: gbpi\n");
	Vector v;
	manager->getProductInfoSet(&v, productId, false); // exclude temporary license
	if (v.size() == 0) 
	{
		// no license is found for specified pid
		LogToVa("sc: gbpi no pid match\n");
		return nullptr;
	}
	
	OrderedProductInfo* infoStandard = nullptr;
	OrderedProductInfo* infoPersonal = nullptr;

	for (int i = 0; i < v.size(); i++)
	{
		OrderedProductInfo* pi = const_cast<OrderedProductInfo*>(reinterpret_cast<const OrderedProductInfo*>(v[i]));
		const int curSkuId = pi->getSkuId();
		if (curSkuId == StandardSku)
			infoStandard = pi;
		else if (curSkuId == PersonalSku)
			infoPersonal = pi;
	}

	if (infoStandard && infoPersonal)
	{
		// see if either is inactive or expired
		// if so, return the other
		if (!manager->isActive(infoPersonal))
			return infoStandard;
		if (!manager->isActive(infoStandard))
			return infoPersonal;

		const String *attr = infoPersonal->getCustomAttributeValue(ExpirationDateAttributeName);
		int expYr2, expMo2, expDay2;
		GetMaintenanceEndDate(attr, expYr2, expMo2, expDay2);
		if (!expYr2 || !expMo2 || !expDay2)
			return infoStandard;

		attr = infoStandard->getCustomAttributeValue(ExpirationDateAttributeName);
		int expYr1, expMo1, expDay1;
		GetMaintenanceEndDate(attr, expYr1, expMo1, expDay1);
		if (!expYr1 || !expMo1 || !expDay1)
			return infoPersonal;

		// compare the 2 exp dates
		if (expYr1 < expYr2)
			return infoPersonal;
		if (expYr2 < expYr1)
			return infoStandard;

		// same year, compare months now
		if (expMo1 < expMo2)
			return infoPersonal;
		if (expMo2 < expMo1)
			return infoStandard;

		// same month, compare days now
		if (expDay1 < expDay2)
			return infoPersonal;
		if (expDay2 < expDay1)
			return infoStandard;

		// default to standard
		return infoStandard;
	}
	
	if (infoStandard)
		return infoStandard;

	if (infoPersonal)
		return infoPersonal;

	LogToVa("sc: gbpi no sku match\n");
	return nullptr;
}

void
SanctuaryClient::cleanupAndExit()
{
	if (!manager)
		return;

	LogToVa("sc: exit\n");
	String def = manager->getDefaultDir();
	File infoFile(manager->getInfoFileName());

	//first check slip import directory for backup file.
	const String s2 = manager->getSlipImportDir();
	File bInfoFile(s2, ".cgb_license");
	bool cleaned = false;
	//clean up backup file first.
	if (bInfoFile.Exists())
	{
		bInfoFile.Delete();
		cleaned = true;
	}
	else
	{
		bInfoFile.SetPathName(def);
		if (bInfoFile.Exists())
		{
			bInfoFile.Delete();
			cleaned = true;
		}
	}

	if (cleaned && infoFile.Exists())
	{
		infoFile.Delete();
	}

	Directory fLicenseDir(def);
	int size = fLicenseDir.size();
	for (int i = 0; i < size; i++)
	{
		const String* s = fLicenseDir[i];
		if (s &&
			((s->startsWith(".") && s->endsWith(".slip")) || (s->startsWith("reg") &&
			(s->endsWith(".slip") || s->endsWith(".txt")))))
		{
			File f(def, s->getChars());
			f.Delete();
		}
	}
}

void
SanctuaryClient::reload()
{
	ClearLastError();
	if (!manager)
		return;

	try
	{
		LogToVa("sc: reload\n");
		manager->load();
		checkProductInfo();
	}
	catch (SlipException& ex)
	{
		String error;
		SanctuaryExt::GetErrorMessage(&error, ex);
		lastError.msg = error;
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::rl 1 %s\n", error.getChars());
		LogToVa(logMsg);
	}
	catch (RuntimeException& ex)
	{
		lastError.msg = ex.what();
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::rl 2 %s\n", ex.what());
		LogToVa(logMsg);
	}
	catch (...)
	{
		LogToVa("ERROR: sclnt::rl 3\n");
	}
}

#if 0
void
SanctuaryClient::report(bool showAll)
{
	if (!manager)
		return;

	String l;
	printf("\n---------------------------------------------------------\n");
	printf("License Details:\n\n");
	if (lockType != SlipManager::LOCK_USER)
	{
		l = "node-locked";
	}
	else
	{
		l = "user-locked";
	}
	printf("Locking Type: %s\n", l.getChars());
	int session = manager->getSessionId();
	printf("System Registration Key: %i\n", session);
	printf("\n\n");

	Vector v;
	manager->getProductInfoSet(&v);
	for (int i = 0; i < v.size(); i++)
	{
		OrderedProductInfo* pi = const_cast<OrderedProductInfo*>((const OrderedProductInfo*)v.get(i));
		if (pi)
		{
			const String* packageTitle = pi->getTitle();
			if (packageTitle)
				printf("License:       %s\n", packageTitle->getChars());
			else
			{
				const String* productName = pi->getProductIdLabel();
				const String* skuName = pi->getSkuLabel();
				printf("License:       %s %s\n", productName->getChars(), skuName->getChars());
			}

			const String* serial = pi->getSerialNo();
			if (serial)
				printf("Serial Number: %s\n", serial->getChars());

			const String* termType = pi->getTermTypeLabel();
			if (termType)
				printf("Term Type: %s\n", termType->getChars());

			if (pi->isTerm() || pi->isEnterpriseInfoExactDate())
			{
				printf("Days Left: %d\n", pi->getDaysLeft());
			}

			if (pi->isEnterpriseInfo())
			{
				printf("Server Name: %s\n", getLicServerName().getChars());

				if (pi->isBeliseInfo() && pi->getBorrowedMillisLeft() > 0)
				{
					printf("Borrowed Millis Left: %f\n", pi->getBorrowedMillisLeft());
				}
			}
			if (showAll)
			{
				PrintAttributes(pi, true);
				PrintAttributes(pi, false);
			}
		}
		printf("\n\n");
	}
	printf("\n---------------------------------------------------------\n");
}

void
SanctuaryClient::PrintAttributes(const OrderedProductInfo* info, bool standard)
{
	if (!info)
		return;

	Vector v;
	int size = 0;
	if (standard)
	{
		info->getStandardAttributes(&v);
		size = info->getStdSize();
	}
	else
	{
		info->getCustomAttributes(&v);
		size = info->getCustomSize();
	}

	if (size == 0)
	{
		printf("No attribute values for current license.\n");
		return;
	}

	if (standard)
	{
		printf("\nListing all standard attributes:\n");
	}
	else
	{
		printf("\nListing all custom attributes:\n");
	}

	for (int i = 0; i < size && i < v.size(); i++)
	{
		const String* key = (const String*)v.get(i);
		if (key)
		{
			String* value = nullptr;
			if (standard)
			{
				value = const_cast<String*>(info->getAttributeValue(*key));
			}
			else
			{
				value = const_cast<String*>(info->getCustomAttributeValue(*key));
			}
			if (value)
				printf("%s => %s\n", key->getChars(), value->getChars());
		}
	}
	v.clear();
}
#endif


//private
void
SanctuaryClient::CheckNetworked()
{
	LogToVa("sc: cn\n");
	int status = checkout();
	int connStatus = info->getLserverStatus();
	if (connStatus != OrderedProductInfo::STATUS_OK &&
		(license != nullptr && info->getBorrowedMillisLeft() > 0))
	{
		handleLicensingError(license->getError(), info->getBorrowedMillisLeft(), true, license);
	}
	else if (license && status != SanctLicense::OK)
	{
		String error;
		GetDetailErrorMessage(&error, license->getError(), license);
		lastError.msg = error;
		CString logMsg;
		CString__FormatA(logMsg, "ERROR: sclnt::cn %s\n", error.getChars());
		LogToVa(logMsg);
		licensed = false;
	}
}

/**
* Prints to console total years until deployment license expiration.
*
**/
void
PrintDeploymentYears(const char* prefix, OrderedProductInfo* info)
{
	if (info == nullptr)
		return;

	String msg(prefix);
	SanctLong remaining = info->getBorrowedMillisLeft();
	SanctLong years = (SanctLong)(remaining / (86400000 * 365.25)); // milliseconds in an average year
	msg.append(years);
	msg.append(" years until it expires.");
	LicenseTraceBox(nullptr, msg.getChars());
}

/**
*  Verifies a deployment license (network license with long heartbeat/borrow time).
*  Designed for use with server based products that do not want to contact
*  license server on a regular basis.  Usual max offline borrow time for non-deployment
*  licenses is 30 days. For deployment licenses, max is length of max long.
*
**/
void
SanctuaryClient::CheckDeployment(const String& _coUserName, const String& _coHostName)
{
	SanctLong remaining = info->getBorrowedMillisLeft();
	if (remaining <= 0)
	{
		SanctLong MAX_VALUE = (SanctLong)0x7fffffffffff0000LL; // Java's Long.MAX_VALUE - ffff
		if (UpdateDeployment(MAX_VALUE, _coUserName, _coHostName) != SanctLicense::OK)
		{
			licensed = false;
		}
	}

	String msg;
	if (remaining > 0)
	{
		msg = "Client was deployed previously and now has ";
	}
	else
	{
		msg = licensed ? "Client is now deployed and has " : "Client failed to deploy and has ";
	}
	PrintDeploymentYears(msg, info);
}

/**
*  Enables API user to release the deployment license from the product
*
**/
void
SanctuaryClient::ReleaseDeployment(const String& _coUserName, const String& _coHostName)
{
	if (UpdateDeployment(0, _coUserName, _coHostName) != SanctLicense::OK)
	{
		licensed = false;
	}

	PrintDeploymentYears(licensed ? "Client is now released and has " : "Client failed to release and has ", info);
}

//private
int
SanctuaryClient::UpdateDeployment(SanctLong duration, const String& _coUserName, const String& _coHostName)
{
	int rc = manager->UpdateDeployment(info, duration, _coUserName, _coHostName);
	if (rc != SanctLicense::OK)
	{
		CString msg;
		CString__FormatA(msg, "ERROR: sclnt::ud fail %d", rc);
		if (gVaInfo)
			gVaInfo->LogStr(msg);
	}
	return rc;
}

void
SanctuaryClient::GetDetailErrorMessage(String* errorMsg, const sanct_error* s_error, const SanctLicense* license)
{
	if (license)
	{
		// info with details
		String address = license->getServerAddress();

		SanctuaryExt::GetErrorMessage(errorMsg, s_error,
			license->getCoUserName(),
			license->getCoHostName(),
			&address);
	}
	else
	{
		// basic info
		SanctuaryExt::GetErrorMessage(errorMsg, s_error);
	}
}

// static void
// LicenseViolationCallback()
// {
// 	Log("OZCB");
// 	CString user(gVaLicensingHost ? gVaLicensingHost->GetLicenseInfoUser() : "");
// 	CString msg;
// 	msg.Format(kMsgBoxText_UserCountExceeded, (const TCHAR *)user);
// 	WtMessageBox(MainWndH, msg, kMsgBoxTitle_UserCountExceeded, MB_OK);
// }

/**
* Callback method called from Heartbeat thread.
* @todo check/modify product specific error handling method
*/
void
SanctuaryClient::handleLicensingError(const sanct_error* s_error, SanctLong remainingTime, bool isBorrowing, SanctLicense* license)
{
	(void)license;
// 	gLicenseCountOk = FALSE;
// 	SmartFlowPhoneHome::FireLicenseCountViolation();
// 	new LambdaThread([] { MonitorViolationCallback(); }, "LVN", true);

	LogToVa("sc: hle\n");
	String errorMsg;
	if (s_error)
	{
		GetDetailErrorMessage(&errorMsg, s_error, nullptr);
		LogToVa("ERROR: sc: ");
		if (errorMsg.getChars())
			LogToVa(errorMsg.getChars());
	}

	String text;
//	int type = 0;
//	int timeout = 0;
	int remainingMin = remainingTime <= 0 ? 0 : (int)(remainingTime / 60000);
	//borrowed license, display warning if less than 60 minutes
	if (isBorrowing)
	{
		if (remainingMin <= 0)
		{
			text.append("License Has Expired");
		}
		else if (remainingMin < 60)
		{
			String expMsg("Client license has ");
			expMsg.append(remainingMin);
			expMsg.append(" minutes until it expires.");

			text = expMsg;
		}

		if (text.length() != 0)
		{
			LicenseTraceBox(nullptr, text.getChars());
			LogToVa("hle 1");
			LogToVa(text.getChars());
		}
	}
	else if (remainingMin > 0)
	{
		// still trying to connect or get license
		LicenseTraceBox(nullptr, errorMsg.getChars());
		LogToVa("hle 2");
		LogToVa(errorMsg.getChars());
	}
	else
	{
		// possible actions: shut down the product, degrade sku,...
		LicenseTraceBox(nullptr, errorMsg.getChars());
		LogToVa("hle 3");
		LogToVa(errorMsg.getChars());
		// #sanctuaryTodo when does this occur?  decide if anything needs to happen here
	}
}

bool
SanctuaryClient::HasProductInfo() const
{
	_ASSERTE(checked);
	return info != nullptr;
}

bool
SanctuaryClient::IsConcurrentLicenseInstalled() const
{
	if (HasProductInfo())
	{
		if (ParamData::SERVER_NONE != licServerType && ParamData::LICTYPE_FLOATING == licType)
			return true;
	}

	return false;
}

bool
SanctuaryClient::IsNamedNetworkLicenseInstalled() const
{
	if (HasProductInfo())
	{
		if (ParamData::SERVER_NONE != licServerType && ParamData::LICTYPE_NAMED == licType)
			return true;
	}

	return false;
}

bool
SanctuaryClient::IsSuperkeyLicenseInstalled() const
{
	if (info)
	{
		/*
		// This detection method does not work for our superkeys, but might be helpful in the future. The reason it does not
		// work right now is that ATTR_SUPERKEY is unset (nullptr) on our superkeys.

		// the SUPERKEY attribute is set for network licenses too, which we want to avoid counting as a "Superkey"
		if (!info->isEnterpriseInfo())
		{
			const String* superKey = info->getCustomAttributeValue(ParamData::ATTR_SUPERKEY);
			return (superKey != nullptr && superKey->equals(ParamData::Yes));
		}
		*/

		const SignedSlip* slip = info->getOwnerPackage();
		return (slip != NULL && slip->getSessionLock() == 0);
	}

	return false;
}

/**
*  Callback handler for Network License Server error conditions.
**/
int
LicenseHandler::handleEvent(int event, SanctLicense& license, const SanctHeartbeat& heartbeat, int rc)
{
	switch (event)
	{
	case SanctHandler::EVENT_ERROR:
		LogToVa("sc: hev err");
		rc = handleError(license, heartbeat);
		break;

	case SanctHandler::EVENT_BORROWING:
		LogToVa("sc: hev bor");
		SanctuaryClient::handleLicensingError(nullptr, license.getBorrowingLeft(), true, &license);
		break;

	case SanctHandler::EVENT_OK:
		LogToVa("sc: hev ok");
		// printf("\n%d %d - Connected\n", license.GetProductInfo()->getProductId(), license.GetProductInfo()->getSkuId());
		break;
	case SanctHandler::EVENT_FINISHED_CHECKIN:
		LogToVa("sc: hev fin");
		// printf("\n%d %d - Finished checkin\n", license.GetProductInfo()->getProductId(), license.GetProductInfo()->getSkuId());
		break;
	}
	return rc;
}

int
LicenseHandler::handleError(SanctLicense& license, const SanctHeartbeat& heartbeat)
{
	LicenseTraceBox(nullptr, "LicenseHandler::handleError");

	const sanct_error* s_error = license.getError();
	SanctLong remainingTime = 0;
	switch (s_error->code)
	{
	case SanctLicense::OK:
		LogToVa("sc: herr ok");
		return SanctHandler::RC_NOACTION;

	case SanctLicense::ERROR_INVALID_LICENSE_FILE: //reason: # flexlm config file or slip file contains bad data
	case SanctLicense::ERROR_NO_LICENSE_ON_SERVER: //reason: license server does not have license for this product
	case SanctLicense::ERROR_EXPIRED_LICENSE:      //reason: # license has expired
	case SanctLicense::ERROR_LIC_UNAVAILABLE:      //reason: # of floating licenses reached limit on the server
	case SanctLicense::ERROR_INVALID_REQUEST:      //reason: invalid request sent to the server
	case SanctLicense::ERROR_INVALID_RESPONSE:     //reason: server sending invalid response
	case SanctLicense::ERROR_ADDRESS_OUT_OF_RANGE: //reason: cannot connect to the server from this IP address
	case SanctLicense::ERROR_USER_UNKNOWN:         //reason: the user is not on the named user list
	case SanctLicense::ERROR_INTERNAL:             //reason: internal error
	case SanctLicense::ERROR_UNKNOWN:              //reason: server error
		LogToVa("sc: herr err");
		SanctuaryClient::handleLicensingError(s_error, 0, false, &license);
		return SanctHandler::RC_DISCONNECT;

	case SanctLicense::ERROR_NO_SERVER_CONNECTION: //reason:  cannot connect to the license server, connection lost
		LogToVa("sc: herr error no server conn");
		remainingTime = heartbeat.getGracePeriodLeft(); //in millis
		SanctuaryClient::handleLicensingError(s_error, remainingTime, false, &license);
		if (remainingTime > 0)
			return SanctHandler::RC_CONNECT;
		else
			return SanctHandler::RC_DISCONNECT;
		break;

	default:
		LogToVa("sc: herr def");
		SanctuaryClient::handleLicensingError(s_error, 0, false, &license);//RES UnknownErrorCode
		return SanctHandler::RC_DISCONNECT;
	}
}

#endif
