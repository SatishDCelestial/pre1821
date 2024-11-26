#pragma once

#include "project.h"

#define SECURITY_STRING_NAME "VADIAGNOSTICLEVEL"

#if defined(_DEBUG) || defined(RAD_STUDIO) || defined(NO_ARMADILLO) || defined(AVR_STUDIO)
// this needs to match the string value of SECURITY_STRING_NAME in the armadillo
// wrapper so that debug builds work properly
#define DEBUG_SECURITY_STRING "ErrorsOnly"
// hash of DEBUG_SECURITY_STRING: 0xde83bc5f
#endif

// Uncomment this to do a special build that logs wrapper events.
// Logging must be enabled via registry so that startup is logged properly.
// #define DEBUG_WRAP_CHECK

class WrapperCheck
{
  protected:
	WrapperCheck()
	{
	}

  public:
	static void Init()
	{
		sWrapperCheckDecoy = true;
	}
	static bool IsOk()
	{
		return sWrapperCheckDecoy;
	}

  protected:
	static bool sWrapperCheckDecoy; // this was cracked - leave but don't really use it
};

extern DWORD gArmadilloSecurityStringValue;

class WrapperCheckDecoy : public WrapperCheck
{
  public:
	WrapperCheckDecoy(UINT& counter, UINT threshold = 128) : mCounter(counter), mThreshold(threshold)
	{
		// junk - want to reference gArmadilloSecurityStringValue in many places
		mWrapperPresent = gArmadilloSecurityStringValue >> 8;
	}

	~WrapperCheckDecoy()
	{
		mCounter++;
		// pointless really - just want to keep mThreshold from being
		// optimized away from the decoys
		if (!mThreshold && mWrapperPresent)
			mCounter++;
	}

  protected:
	UINT& mCounter;
	DWORD mWrapperPresent;
	const UINT mThreshold;
};

// this has been cracked - leave it but don't rely on it
class CrackedWrapperCheck : public WrapperCheckDecoy
{
  public:
	CrackedWrapperCheck(UINT& counter, UINT threshold) : WrapperCheckDecoy(counter, threshold)
	{
		mWrapperPresent = (gArmadilloSecurityStringValue >> 8) & 0xFFFF;
	}

	~CrackedWrapperCheck()
	{
#if !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(AVR_STUDIO)
		if (mCounter > mThreshold)
		{
			if (mWrapperPresent != 0x83BC)
			{
				sWrapperCheckDecoy = false;
			}
		}
#endif
	}

  private:
	CrackedWrapperCheck();
};

class RealWrapperCheck : public WrapperCheckDecoy
{
  public:
	RealWrapperCheck(UINT& counter, UINT threshold) : WrapperCheckDecoy(counter, threshold)
	{
		mWrapperPresent = gArmadilloSecurityStringValue >> 16;
	}

	~RealWrapperCheck()
	{
#if !defined(RAD_STUDIO) && !defined(NO_ARMADILLO) && !defined(AVR_STUDIO)
#ifdef DEBUG_WRAP_CHECK
		vLog("rwc: %x %x", gArmadilloSecurityStringValue, mWrapperPresent);
#endif
		if (mCounter > mThreshold)
		{
			if (GlobalProject && mWrapperPresent != 0xde83)
			{
#ifdef DEBUG_WRAP_CHECK
				vLog("rwc: false");
#endif
				GlobalProject->WrapperMissing();
				// setting this flag will make us stop using NEW_SCOPE,
				// prevent minihelp from appearing, stop outline updates,
				// and stop HCB updates

				// we could also remove files from our project file list

				// let's not mess with Psettings->m_validLicense just yet...
			}
		}
#endif
	}

  private:
	RealWrapperCheck();
};
