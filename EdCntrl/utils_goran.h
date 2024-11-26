#pragma once

#include <memory>
#include <string>
#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS 1
#include <hash_map>
#include <hash_set>
#include <map>

typedef unsigned int uint;

// correctly pastes __LINE__ to form an identifier
#define myBOOST_JOIN(X, Y) myBOOST_DO_JOIN(X, Y)
#define myBOOST_DO_JOIN(X, Y) myBOOST_DO_JOIN2(X, Y)
#define myBOOST_DO_JOIN2(X, Y) X##Y

class __hold_value_base
{
  public:
	virtual ~__hold_value_base()
	{
	}
};
template <typename TYPE> class __hold_value : public __hold_value_base
{
  public:
	__hold_value(TYPE& value) : value(value)
	{
		old_value = value;
	}
	virtual ~__hold_value()
	{
		value = old_value;
	}

  protected:
	TYPE& value;
	TYPE old_value;
};
template <typename TYPE> __hold_value<TYPE>* __make_hold_value(TYPE& value)
{
	return new __hold_value<TYPE>(value);
}
// on scope exit, return variable to its original value
#define hold_value(value)                                                                                              \
	std::unique_ptr<__hold_value_base> myBOOST_JOIN(__hold_value_holder, __LINE__)(__make_hold_value(value))

// mainly an auto_ptr class that will automatically instance class on first access
// use INIT=true if auto_instance is used as global variable and you have global variable initialization order issue
template <typename TYPE, bool INIT> class auto_instance
{
  public:
	auto_instance()
	{
		if (INIT)
			value = NULL;
	}
	~auto_instance()
	{
		if (value)
		{
			delete value;
			value = NULL;
		}
	}

	void init()
	{
		if (!value)
			value = new TYPE;
	}

	TYPE& operator*()
	{
		init();
		return *value;
	}
	const TYPE& operator*() const
	{
		init();
		return *value;
	}
	TYPE& get()
	{
		return **this;
	}
	const TYPE& get() const
	{
		return **this;
	}
	TYPE* operator->()
	{
		return &**this;
	}
	const TYPE* operator->() const
	{
		return &**this;
	}

  protected:
	TYPE* value;
};

// for case insensitive maps/sets
class iless /*: public binary_function<wstring, wstring, bool>*/
{
  public:
	bool operator()(const std::string& left, const std::string& right) const
	{
		return _stricmp(left.c_str(), right.c_str()) < 0;
	}

	bool operator()(const std::wstring& left, const std::wstring& right) const
	{
		return _wcsicmp(left.c_str(), right.c_str()) < 0;
	}
};

// returns true if map contains given key
template <typename KEY, typename VALUE>
inline bool contains(const stdext::hash_map<KEY, VALUE>& col,
                     const typename stdext::hash_map<KEY, VALUE>::key_type& key)
{
	return col.find(key) != col.end();
}
template <typename KEY>
inline bool contains(const stdext::hash_set<KEY>& col, const typename stdext::hash_set<KEY>::key_type& key)
{
	return col.find(key) != col.end();
}
template <typename KEY, typename VALUE, typename LESS>
inline bool contains(const std::map<KEY, VALUE, LESS>& col, const typename std::map<KEY, VALUE, LESS>::key_type& key)
{
	return col.find(key) != col.end();
}
template <typename VALUE>
inline bool contains(const std::list<VALUE>& col, const typename std::list<VALUE>::value_type& value)
{
	return std::find(col.begin(), col.end(), value) != col.end();
}

// __locks given CCriticalSection during current scope
#if _MSC_VER < 1400
#define __lock(cs) CSingleLock __lock(&(cs), true)
#else
#define __lock(cs) CSingleLock myBOOST_JOIN(__lock, __LINE__)(&(cs), true)
#endif

// returns a number of elements in an array
#define countof(x) (sizeof((x)) / sizeof((x)[0]))
