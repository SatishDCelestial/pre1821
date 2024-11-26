#include "stdafxed.h"

#if !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(VA_CPPUNIT) && !defined(AVR_STUDIO)
#include "phdl.h"
#include "SmartFlow/Include/PhdlLibraryExports.h"
#include "SmartFlow/Helper/Encryption.h"
#include "../BuildInfo.h"
#include "../RegKeys.h"
#include "DllNames.h"
#include "IVaLicensing.h"
#include "../WTKeyGen/WTValidate.h"
#include "FILE.H"
#include "Directories.h"
#pragma warning(push, 1)
#include <atlenc.h>
#pragma warning(pop)
#include "PooledThreadBase.h"
#include <algorithm>
#include <random>
#include "DevShellService.h"
#include "Registry.h"

#ifdef _DEBUG
#define SF_TEST_MODE
#endif

#ifdef SF_TEST_MODE
#include "phdl_config_test.h"
#else
#include "phdl_config_production.h"
#endif

namespace SmartFlowPhoneHome
{

#define PHDK_CLIENT_ENCRYPTION_KEY_LEN 32

#ifdef UNICODE
using tchar = wchar_t;
#define _TT(x) _T(x)
#define Phdl_FreeTchar Phdl_FreeWchar
#else
using tchar = char;
#define _TT(x) x
#define Phdl_FreeTchar Phdl_FreeChar
#endif

#define PHDL_ERROR -2

enum class PhdlTrigger
{
	none,
	missingArmadillo,
	networkCountViolation,
	invalidTerminationDate,
	clockBack,
	clockForward,
	armadilloExpired,
	cancelExpiredLicense,
	cancelTrial,
	renamedVaxDll,
	missingHost
};

int InitializePhdl();
void FirePhdlEvent(Phdl_EnumEventType evtType, PhdlTrigger trigger);
int SetPhdlInfo(Phdl_EnumEventType evtType, PhdlTrigger trigger);

void FireLicenseCountViolation()
{
	// 	WtMessageBox(nullptr, "FireLicenseCountViolation", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Undefined, PhdlTrigger::networkCountViolation); }, "FLC", true);
}

void FireInvalidLicenseTerminationDate()
{
	// 	WtMessageBox(nullptr, "FireInvalidLicenseTerminationDate", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Unlicensed, PhdlTrigger::invalidTerminationDate); }, "FLD",
	                 true);
}

void FireRenamedVaxDll()
{
	// 	WtMessageBox(nullptr, "FireRenamedVaxDll", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Undefined, PhdlTrigger::renamedVaxDll); }, "FRD", true);
}

void FireArmadilloMissing()
{
	// 	WtMessageBox(nullptr, "FireArmadilloMissing", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::UnlicensedProjectFile, PhdlTrigger::missingArmadillo); },
	                 "FAM", true);
}

void FireArmadilloClockBack()
{
	// 	WtMessageBox(nullptr, "FireArmadilloClockBack", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Undefined, PhdlTrigger::clockBack); }, "FCB", true);
}

void FireArmadilloClockForward()
{
	// 	WtMessageBox(nullptr, "FireArmadilloClockForward", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Undefined, PhdlTrigger::clockForward); }, "FCF", true);
}

void FireArmadilloExpired()
{
	// 	WtMessageBox(nullptr, "FireArmadilloExpired", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Evaluation, PhdlTrigger::armadilloExpired); }, "FAE", true);
}

void FireCancelExpiredLicense()
{
	// 	WtMessageBox(nullptr, "FireCancelExpiredLicense", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Legal, PhdlTrigger::cancelExpiredLicense); }, "FCE",
	                 true);
}

void FireCancelTrial()
{
	// 	WtMessageBox(nullptr, "FireCancelTrial", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Evaluation, PhdlTrigger::cancelTrial); }, "FCT", true);
}

void FireMissingHost()
{
	// 	WtMessageBox(nullptr, "FireMissingHost", "Sean");
	new LambdaThread([] { FirePhdlEvent(Phdl_EnumEventType::Undefined, PhdlTrigger::missingHost); }, "FMH", true);
}

const int DEFAULT_MATCH_DAY = 3;
const int LICENSE_CHECK_INTERVAL = 5; // fire once in 5 days
static DWORD sRecordDate = 0;

#define CHECK_DATE "checkVersionData"

bool DayInMatchDays(int Day, int MatchDay, int DaysInMonth)
{
	while (Day > MatchDay && MatchDay < DaysInMonth)
		MatchDay = MatchDay + LICENSE_CHECK_INTERVAL;
	return Day == MatchDay;
}

bool OkToSendNotification()
{
	Sleep(5000);

	// treat volume serial number as user id
	DWORD sn = 0;
	CStringW dir(VaDirs::GetDllDir());
	if (!dir.IsEmpty() && dir.GetLength() > 3 && dir[1] == ':' && (dir[2] == '\\' || dir[2] == '/'))
	{
		dir = dir.Left(3);
		GetVolumeInformationW(dir, nullptr, 0, &sn, nullptr, nullptr, nullptr, 0);
	}

	// user id determines match-day
	int matchDay = DEFAULT_MATCH_DAY;
	if (sn)
		matchDay = int(sn % LICENSE_CHECK_INTERVAL) + 1;

	// get year, month, day from current date
	const CTime curDate = CTime::GetCurrentTime();
	const int curYear = curDate.GetYear();
	const int curMonth = curDate.GetMonth();
	int curDay = curDate.GetDay();

	// read the previous smartflow call that we have recorded in the registry.
	const DWORD lastDayChecked = GetRegDword(HKEY_CURRENT_USER, ID_RK_APP, CHECK_DATE, 0);

	// extract year, month, day from previous date DWORD; see:#phdlDateCompression
	const int prevCheckYear = int(lastDayChecked >> 16);
	const int prevCheckMonth = int(lastDayChecked & 0xff);
	const int prevCheckDay = int((lastDayChecked >> 8) & 0xff);

	// do not send more than one notification per day
	if (prevCheckYear == curYear && prevCheckMonth == curMonth && prevCheckDay == curDay)
		return false;

	// how may days in the current date
	//							   J   F   M   A   M   J   J   A   S   O   N   D
	const WORD kMonthMaxDays[] = {31, 27, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	_ASSERTE(curMonth > 0 && curMonth < 13);
	const int daysInMonth = kMonthMaxDays[curMonth - 1];

	// This logic is mostly from Hin Boen, as used in RAD Studio.
	// check if today is a match day
	if (DayInMatchDays(curDay, matchDay, daysInMonth))
	{
		// today is a reporting day
	}
	else
	{
		// if today is not a match day, see if we have called smartflow in the previous match day; maybe
		// user did not run the product on the previous match day.

		// if no previous call found, we assume previous smartflow call is today

		// if same year, and month, see if previous smartflow call is in match day, if it does, advance what the next
		// match day is supposed to be
		if ((curYear == prevCheckYear) && (curMonth == prevCheckMonth) &&
		    DayInMatchDays(prevCheckDay, matchDay, daysInMonth))
			matchDay = prevCheckDay + LICENSE_CHECK_INTERVAL;

		// if today is not the match day, do nothing, and exit
		if (curDay < matchDay)
		{
			if (prevCheckYear > curYear)
				; // odd, just go ahead w/ notify and then reset lastDayChecked
			else
				return false;
		}

		// just in case user has not run the product on the previous match day, calculate what is the previous match day
		// supposed to be. We should have called smartflow on that previous match day, but we did not
		while ((matchDay + LICENSE_CHECK_INTERVAL) < curDay)
			matchDay = matchDay + LICENSE_CHECK_INTERVAL;

		// save the date where we will be calling smartflow
		curDay = matchDay;
	}

	// #phdlDateCompression record the date/time where we call smartflow
	sRecordDate = DWORD((curYear << 16) | (curDay << 8) | curMonth);

	return true;
}

void RecordNotificationSent()
{
	if (sRecordDate)
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, CHECK_DATE, sRecordDate);
}

void FirePhdlEvent(Phdl_EnumEventType evtType, PhdlTrigger trigger)
{
	if (!OkToSendNotification())
		return;

	/*
	Phdl_IO : Phdl_InitializeOptions
	Phdl_IC : Phdl_InitializeConfiguration
	Phdl_GLV : Phdl_GetLibraryVersion
	Phdl_SAC : Phdl_SetAllowCapture
	Phdl_StaCS : Phdl_StartCaptureSession
	Phdl_SIF : Phdl_SetInformationField
	Phdl_SDOTF : Phdl_SendDataOnTheFly
	Phdl_StoCS : Phdl_StopCaptureSession
	Phdl_PIC : Phdl_PerformInternalCleanup
	*/

	InitializePhdl();

	tchar* phdlVersion;
	int result = PHDL_ERROR;
#ifdef UNICODE
	result = Phdl_GLVW(&phdlVersion);
#else
	result = Phdl_GLV(&phdlVersion);
#endif
	if (PHDL_SUCCESS == result)
	{
		result = SetPhdlInfo(evtType, trigger);
		if (PHDL_SUCCESS == result)
			result = Phdl_SDOTF();
	}

	Phdl_FreeTchar(phdlVersion);
	phdlVersion = nullptr;

	if (PHDL_SUCCESS == result)
		RecordNotificationSent();

	result = Phdl_PIC();
}

int InitializePhdl()
{
	unsigned char buffer[PHDK_CLIENT_ENCRYPTION_KEY_LEN];
	int destLen = PHDK_CLIENT_ENCRYPTION_KEY_LEN;
	int retVal;
	tchar* dirPath = nullptr;
	tchar* temp = nullptr;
#ifdef UNICODE
	PhdlOptionsW options;
	retVal = Phdl_IOW(&options);
#else
	PhdlOptions options;
	Phdl_IO(&options);
#endif
	options.ProductName = _tcsdup(_TT(IDS_APPNAME));
	options.ProductVersion = _tcsdup(_TT(PRODUCT_VERSION_STRING));
	options.EncryptionKey = buffer;
	options.IncludeInstalledApplications = 0;
	options.UseRetryBasedBackupURLs = 0;
	options.SendRetryCount = 3;
	options.SendRetryIntervalMs = 1000;
	options.TimeBetweenCapturesSec = 2 * 60 * 60;
	options.TimeBetweenSendSec = 2 * 60 * 60;

	unsigned char* key = PhdlHelper_GetKey(PHDL_KEY_SEED);
	char* decryptedUsername = PhdlHelper_DecryptString(PHDL_ACCOUNT_NAME, key);

#ifdef UNICODE
	options.Username = PhdlHelper_UTF8_Decode(decryptedUsername);
	if (decryptedUsername)
	{
		ZeroMemory(decryptedUsername, strlen(decryptedUsername));
		free(decryptedUsername);
	}
#else
	options.Username = decryptedUsername;
#endif

	char* decryptedPassword = PhdlHelper_DecryptString(PHDL_PASSWORD, key);
#ifdef UNICODE
	options.Password = PhdlHelper_UTF8_Decode(decryptedPassword);
	free(decryptedPassword);
#else
	options.Password = decryptedPassword;
#endif

	std::random_device rd;
	std::mt19937 g(rd());
	char* decryptedUrls[3];
	std::vector<int> indexes = {0, 1, 2};
	std::shuffle(indexes.begin(), indexes.end(), g);
	decryptedUrls[indexes[0]] = PhdlHelper_DecryptString(PHDL_SERVER_URL1, key);
	decryptedUrls[indexes[1]] = PhdlHelper_DecryptString(PHDL_SERVER_URL2, key);
	decryptedUrls[indexes[2]] = PhdlHelper_DecryptString(PHDL_SERVER_URL3, key);
	if (key)
	{
		ZeroMemory(key, AES_KEY_SIZE);
		free(key);
		key = nullptr;
	}

#ifdef UNICODE
	char* tmp = PhdlHelper_UTF8_Decode(decryptedUrls[0]);
	free(decryptedUrls[0]);
	decryptedUrls[0] = tmp;

	tmp = PhdlHelper_UTF8_Decode(decryptedUrls[1]);
	free(decryptedUrls[1]);
	decryptedUrls[1] = tmp;

	tmp = PhdlHelper_UTF8_Decode(decryptedUrls[2]);
	free(decryptedUrls[2]);
	decryptedUrls[2] = tmp;
#endif

	tchar* serverUrls[] = {decryptedUrls[0], decryptedUrls[1], decryptedUrls[2]};

	options.NumberOfServerUrls = 3;
	options.ArrayOfServerUrls = serverUrls;

	options.PathToStorageFile = nullptr;
	// #ifdef _WIN32
	// 	dirPath = (tchar *)malloc((_tcslen(components->Drive) + _tcslen(components->DirPath) + 1) * sizeof(tchar));
	// 	if (!dirPath)
	// 		return 10;
	// 	_tcscpy(dirPath, components->Drive);
	// 	_tcscat(dirPath, components->DirPath);
	// #else
	// 	dirPath = (tchar *)malloc((_tcslen("/") + _tcslen(components->DirPath) + 1) * sizeof(tchar));
	// 	strcpy(dirPath, components->DirPath);
	// 	strcat(dirPath, "/");
	// #endif
	//
	// 	temp = (tchar *)malloc((_tcslen(dirPath) + _tcslen(_TT("storage")) + 1) * sizeof(tchar));
	// 	if (!temp)
	// 	{
	// 		retVal = 10;
	// 		goto Cleanup;
	// 	}
	//
	// 	_tcscpy(temp, dirPath);
	// 	_tcscat(temp, _TT("storage"));
	//
	// 	options.PathToStorageFile = _tcsdup(temp);

#if defined(__linux__) || defined(__APPLE__)
	options.StoreServerDataOnDisk = 1;
#endif

	Base64Decode(PHDL_CLIENT_ENCRYPTION_KEY, strlen_i(PHDL_CLIENT_ENCRYPTION_KEY), buffer, &destLen);

#ifdef UNICODE
	retVal = Phdl_ICW(&options);
#else
	retVal = Phdl_IC(&options);
#endif

	for (size_t index = 0; index < options.NumberOfServerUrls; index++)
	{
		ZeroMemory(serverUrls[index], _tcslen(serverUrls[index]) * sizeof(tchar));
		free(serverUrls[index]);
	}

	free(options.ProductName);
	free(options.ProductVersion);
	// 	free(options.PathToStorageFile);
	if (temp)
		free(temp);
	if (dirPath)
		free(dirPath);

	ZeroMemory(buffer, PHDK_CLIENT_ENCRYPTION_KEY_LEN);

	if (options.Username)
	{
		ZeroMemory(options.Username, _tcslen(options.Username) * sizeof(tchar));
		free(options.Username);
	}

	if (options.Password)
	{
		ZeroMemory(options.Password, _tcslen(options.Password) * sizeof(tchar));
		free(options.Password);
	}

	return retVal;
}

int SetPhdlInfo(Phdl_EnumEventType evtType, PhdlTrigger trigger)
{
	int result = PHDL_ERROR;
	CString tmp;
	bool isAcademic = false;

	if (gVaLicensingHost)
	{
		CStringW user(gVaLicensingHost->GetLicenseInfoUser());
		if (!user.IsEmpty())
		{
#if 1
			result = Phdl_SIFW(EnumPhdlInfoField::LicensedTo, user);
#else
			result = Phdl_SIF(EnumPhdlInfoField::LicensedTo, user);
#endif
			if (PHDL_SUCCESS != result)
				return result;
		}

		if (gVaLicensingHost->IsAcademicLicenseInstalled())
			isAcademic = true;
		else if (gVaLicensingHost->IsNonRenewableLicenseInstalled())
			tmp = _T("nr;");
		else if (gVaLicensingHost->IsNonPerpetualLicenseInstalled())
			tmp = _T("np;");

		DWORD chkSum;
		HashFile(VaDirs::GetDllDir() + IDS_VAX_DLLW, &chkSum);
		CString__AppendFormatA(tmp, "cs=%lx;", chkSum);

#ifdef UNICODE
		result = Phdl_SIFW(EnumPhdlInfoField::Custom1, tmp);
#else
		result = Phdl_SIF(EnumPhdlInfoField::Custom1, tmp);
#endif
		if (PHDL_SUCCESS != result)
			return result;
	}

	CString triggerTxt;
	if (evtType != Phdl_EnumEventType::Undefined)
		; // explicit event type has been passed in
	else if (!gVaLicensingHost)
		evtType = Phdl_EnumEventType::Undefined;
	else if (isAcademic)
		evtType = Phdl_EnumEventType::Educational;
	else
	{
		const int license = gVaLicensingHost ? gVaLicensingHost->GetLicenseStatus() : -1;
		if (LicenseStatus_Valid == license)
			evtType = Phdl_EnumEventType::Legal;
		else if (LicenseStatus_Invalid == license && gVaLicensingHost->GetLicenseUserCount() > 0)
			evtType = Phdl_EnumEventType::Unlicensed;
		else if (LicenseStatus_Invalid == license)
			evtType = Phdl_EnumEventType::Evaluation;
		else if (LicenseStatus_Expired == license)
			evtType = Phdl_EnumEventType::Evaluation;
		else if (LicenseStatus_NoFloatingSeats == license || LicenseStatus_SanctuaryElcError == license ||
		         LicenseStatus_SanctuaryError == license)
			evtType = Phdl_EnumEventType::Legal;
		else if (gVaLicensingHost->IsArmExpired())
			evtType = Phdl_EnumEventType::Unlicensed;
		else
		{
			evtType = Phdl_EnumEventType::Undefined;
			if (LicenseStatus_Obsolete == license)
				triggerTxt += _T("ob;");
			else
				triggerTxt += _T("ns;");
		}
	}
#ifdef UNICODE
	result = Phdl_SIFW(EventType, &evtType);
#else
	result = Phdl_SIF(EventType, &evtType);
#endif
	if (PHDL_SUCCESS != result)
		return result;

	if (gVaLicensingHost)
	{
		tmp = gVaLicensingHost->GetLicenseInfoKey();
		if (!tmp.IsEmpty())
		{
#ifdef UNICODE
			result = Phdl_SIFW(EnumPhdlInfoField::LicenseSerialNumber, tmp);
#else
			result = Phdl_SIF(EnumPhdlInfoField::LicenseSerialNumber, tmp);
#endif
			if (PHDL_SUCCESS != result)
				return result;
		}

		tmp = gVaLicensingHost->GetLicenseExpirationDate();
		if (!tmp.IsEmpty())
		{
#ifdef UNICODE
			result = Phdl_SIFW(EnumPhdlInfoField::LicenseExpiryDate, tmp);
#else
			result = Phdl_SIF(EnumPhdlInfoField::LicenseExpiryDate, tmp);
#endif
			if (PHDL_SUCCESS != result)
				return result;
		}
	}

	switch (trigger)
	{
	case PhdlTrigger::networkCountViolation:
		triggerTxt += _T("nc;");
		break;
	case PhdlTrigger::missingArmadillo:
		triggerTxt += _T("ma;");
		break;
	case PhdlTrigger::invalidTerminationDate:
		triggerTxt += _T("td;");
		break;
	case PhdlTrigger::clockBack:
		triggerTxt += _T("cb;");
		break;
	case PhdlTrigger::clockForward:
		triggerTxt += _T("cf;");
		break;
	case PhdlTrigger::armadilloExpired:
		triggerTxt += _T("ae;");
		break;
	case PhdlTrigger::cancelExpiredLicense:
		triggerTxt += _T("ce;");
		break;
	case PhdlTrigger::cancelTrial:
		triggerTxt += _T("ct;");
		break;
	case PhdlTrigger::renamedVaxDll:
		triggerTxt += _T("rd;");
		break;
	case PhdlTrigger::missingHost:
		triggerTxt += _T("mh;");
		break;

	case PhdlTrigger::none:
	default:
#ifdef _DEBUG
		triggerTxt += _T("Visual Assist test trigger;");
#else
		triggerTxt += _T("no;");
#endif
		break;
	}

	if (!gVaLicensingHost)
		triggerTxt += _T("nh;");

	if (!triggerTxt.IsEmpty())
	{
#ifdef UNICODE
		result = Phdl_SIFW(EnumPhdlInfoField::Trigger, triggerTxt);
#else
		result = Phdl_SIF(EnumPhdlInfoField::Trigger, triggerTxt);
#endif
		if (PHDL_SUCCESS != result)
			return result;
	}

	return result;
}

} // namespace SmartFlowPhoneHome

#else

namespace SmartFlowPhoneHome
{
void FireLicenseCountViolation()
{
}

void FireInvalidLicenseTerminationDate()
{
}

void FireRenamedVaxDll()
{
}

void FireArmadilloMissing()
{
}

void FireArmadilloClockBack()
{
}

void FireArmadilloClockForward()
{
}

void FireArmadilloExpired()
{
}

void FireCancelExpiredLicense()
{
}

void FireCancelTrial()
{
}

void FireMissingHost()
{
}
} // namespace SmartFlowPhoneHome

#endif // RAD_STUDIO || NO_ARMADILLO || VA_CPPUNIT || AVR_STUDIO
