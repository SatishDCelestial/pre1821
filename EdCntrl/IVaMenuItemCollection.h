#pragma once

class IVaMenuItem
{
  public:
	virtual const wchar_t* Text() const = 0;
	virtual void* Icon(bool moniker) = 0;
	virtual bool IsSeparator() const = 0;
	virtual bool IsEnabled() const = 0;

	virtual void Invoke() = 0;

  protected:
	virtual ~IVaMenuItem() = default;
	IVaMenuItem() = default;
	IVaMenuItem(const IVaMenuItem&) = default;
	IVaMenuItem& operator=(const IVaMenuItem&) = default;
};

class IVaMenuItemCollection
{
  public:
	virtual void Release() = 0;

	virtual int Count() const = 0;
	virtual IVaMenuItem* Get(int n) = 0;

  protected:
	virtual ~IVaMenuItemCollection() = default;
	IVaMenuItemCollection() = default;
	IVaMenuItemCollection(const IVaMenuItemCollection&) = default;
	IVaMenuItemCollection& operator=(const IVaMenuItemCollection&) = default;
};
