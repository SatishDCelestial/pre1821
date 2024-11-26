#pragma once

// This class is a base class of CEdDllApp but can be used standalone.
// Created so that CEdDllApp does not need to be used during testing.
// Bypasses licensing checks and allows setup/breakdown without process restart.

class VaApp
{
	HMODULE mVaDllLoadAddress;
	bool mAuto;
#ifdef _DEBUG
	CMemoryState m_memStart, m_memEnd, m_memDiff;
#endif

  public:
	VaApp(bool autoStart) : mVaDllLoadAddress(NULL), mAuto(autoStart)
	{
		if (mAuto)
			Start();
	}
	~VaApp()
	{
		if (mAuto)
			Exit();
	}

	void Start();
	void Exit();

	HMODULE GetVaAddress() const
	{
		return mVaDllLoadAddress;
	}
};
