#pragma once

#include "WTString.h"

class WtException
{
  public:
	WtException(const WTString& desc) : mDesc(desc)
	{
	}
	WtException(LPCTSTR desc) : mDesc(desc)
	{
	}
	WtException(const WtException& desc)
	    : mDesc(desc.mDesc)
	{
	}
	virtual ~WtException() = default;

	const WTString& GetDesc() const
	{
		return mDesc;
	}

  private:
	WTString mDesc;
};

class UnloadingException : public WtException
{
  public:
	UnloadingException() : WtException("unloading")
	{
	}
	UnloadingException(const WTString& desc) : WtException(desc)
	{
	}
	UnloadingException(LPCTSTR desc) : WtException(desc)
	{
	}
	~UnloadingException()
	{
	}
};
