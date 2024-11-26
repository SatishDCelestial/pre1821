#pragma once

#include "AutoUpdater.h"

#if !defined(RAD_STUDIO) && !defined(VA_CPPUNIT)
class WTAutoUpdater : public CAutoUpdater
{
  public:
	WTAutoUpdater(void);
	~WTAutoUpdater(void);
	void CheckForUpdate(BOOL checkForBeta, BOOL manualCheck);
	void CheckForLicenseExpiration();

  private:
	BOOL GetCurrentBuildNumbers(bool getRealLatestBuild, INT& curRelease, INT& curReleaseSpecial, INT& curBeta,
	                            INT& curBetaSpecial);
	CString mSupportExp;
	INT mThisBuild = 0, mThisBuildSpecial = 0;
};
#endif

void CheckForLatestVersion(BOOL doManualCheck);
void CheckIDENavigationBar();
void CheckNavBarNextReminder();
