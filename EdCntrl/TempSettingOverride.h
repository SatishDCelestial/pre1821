#pragma once

template <typename T, typename STORAGE = T> class TempSettingOverride
{
	T* mSetting;
	STORAGE mOldValue;
	STORAGE mNewValue;
	bool mCondition;

  public:
	TempSettingOverride(T* theSetting, STORAGE newValue = 0, bool condition = true)
	    : mSetting(theSetting), mOldValue(*theSetting), mNewValue(newValue), mCondition(condition)
	{
		if (mCondition)
			*mSetting = mNewValue;
	}

	~TempSettingOverride()
	{
		Restore();
	}

	void Restore()
	{
		if (mCondition)
		{
			*mSetting = mOldValue;
			mCondition = false;
		}
	}
};
