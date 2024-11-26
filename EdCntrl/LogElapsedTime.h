#pragma once

#include "WTString.h"
#include "LOG.H"

// Class for conditionally reporting duration of a block operation.

class LogElapsedTime
{
  public:
	LogElapsedTime(const char* name, DWORD reportThreshold = 400)
	    :
#if defined(SEAN)
	      mEnable(true)
#else
	      mEnable(!!g_loggingEnabled)
#endif
	{
		if (!mEnable)
			return;

		mText = name;
		mReportingThreshold = reportThreshold;
		mStartTime = ::GetTickCount();
	}

	LogElapsedTime(const char* name, const WTString& moreText, DWORD reportThreshold = 400)
	    :
#if defined(SEAN)
	      mEnable(true)
#else
	      mEnable(!!g_loggingEnabled)
#endif
	{
		if (!mEnable)
			return;

		mText = name;
		AddText(moreText);
		mReportingThreshold = reportThreshold;
		mStartTime = ::GetTickCount();
	}

	LogElapsedTime(const char* name, const CStringW& moreText, DWORD reportThreshold = 400)
	    :
#if defined(SEAN)
	      mEnable(true)
#else
	      mEnable(!!g_loggingEnabled)
#endif
	{
		if (!mEnable)
			return;

		mText = name;
		AddText(WTString(moreText));
		mReportingThreshold = reportThreshold;
		mStartTime = ::GetTickCount();
	}

	void Enable(bool en)
	{
		mEnable = en;
	}

	void AddText(const WTString& txt)
	{
		if (mEnable)
			mText += " " + txt;
	}

	~LogElapsedTime()
	{
		if (!mEnable)
			return;

		const DWORD endTime = ::GetTickCount();
		if ((endTime - mStartTime) > mReportingThreshold)
		{
			WTString msg;
			msg.WTFormat("TimeThreshold %s %ld ticks\n", mText.c_str(), endTime - mStartTime);
			vCatLog("LogSystem.TimeTracking", "%s", msg.c_str());
			::OutputDebugString(msg.c_str());
		}
	}

  private:
	WTString mText;
	DWORD mStartTime;
	DWORD mReportingThreshold;
	bool mEnable;
};
