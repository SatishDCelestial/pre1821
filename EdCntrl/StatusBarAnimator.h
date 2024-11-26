#pragma once

#include "IVaShellService.h"
#include "StatusWnd.h"

class StatusBarAnimator
{
  public:
	StatusBarAnimator(IVaShellService::StatusAnimation anType, bool async = true)
	    : mDoClear(true), mAsync(async), mType(anType)
	{
		On();
	}
	~StatusBarAnimator()
	{
		Off();
	}

	void On()
	{
		if (g_statBar)
			g_statBar->SetAnimation(mType, mAsync);
	}

	void Off()
	{
		if (g_statBar && mDoClear)
		{
			mDoClear = false;
			g_statBar->ClearAnimation(mAsync);
		}
	}

  private:
	bool mDoClear;
	bool mAsync;
	IVaShellService::StatusAnimation mType;
};

class StatusBarFindAnimation : public StatusBarAnimator
{
  public:
	StatusBarFindAnimation() : StatusBarAnimator(IVaShellService::sa_find)
	{
	}
};

class StatusBarGeneralAnimation : public StatusBarAnimator
{
  public:
	StatusBarGeneralAnimation(bool async = true) : StatusBarAnimator(IVaShellService::sa_general, async)
	{
	}
};

class StatusBarBuildAnimation : public StatusBarAnimator
{
  public:
	StatusBarBuildAnimation(bool async = true) : StatusBarAnimator(IVaShellService::sa_build, async)
	{
	}
};
