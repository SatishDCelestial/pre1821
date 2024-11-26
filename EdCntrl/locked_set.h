#pragma once

#include "utils_goran.h"

#include <set>
using std::set;

template <typename TYPE> class locked_set
{
  public:
	void insert(const TYPE& val)
	{
		__lock(cs);
		col.insert(val);
	}

	void erase(const TYPE& val)
	{
		__lock(cs);
		col.erase(val);
	}

	bool contains(const TYPE& val) const
	{
		__lock(cs);
		return col.find(val) != col.end();
	}

  protected:
	set<TYPE> col;
	mutable CCriticalSection cs;
};
