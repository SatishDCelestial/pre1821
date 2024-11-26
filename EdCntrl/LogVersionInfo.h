#pragma once

class LogVersionInfo
{
  public:
	//#if defined(VA_CPPUNIT)
	//	LogVersionInfo() { }
	//	LogVersionInfo(bool) { }
	//#else
	LogVersionInfo();
	LogVersionInfo(bool all, bool os = false);
	//#endif

	static bool IsX64()
	{
		return mX64;
	}
	static void ForceNextUpdate()
	{
		mForceLoad = true;
	}

  private:
	void CollectInfo();

	CString GetWinDisplayVersion();
	DWORD GetOSInfo();
	void GetDevStudioInfo();
	void GetRegionInfo();
	CString mInfo;
	static bool mX64;
	static bool mForceLoad;
};

CString GetVaVersionInfo();
