#pragma once

#include <ole2.h>
#include <atlconv.h>

class WTComBSTR
{
  public:
	BSTR m_str;
	WTComBSTR()
	{
		m_str = NULL;
	}
	/*explicit*/ WTComBSTR(UINT nSize)
	{
		m_str = ::SysAllocStringLen(NULL, nSize);
	}
	/*explicit*/ WTComBSTR(UINT nSize, LPCOLESTR sz)
	{
		m_str = ::SysAllocStringLen(sz, nSize);
	}
	/*explicit*/ WTComBSTR(LPCOLESTR pSrc)
	{
		m_str = ::SysAllocString(pSrc);
	}
	/*explicit*/ WTComBSTR(const WTComBSTR& src)
	{
		m_str = src.Copy();
	}
	/*explicit*/ WTComBSTR(REFGUID src)
	{
		OLECHAR szGUID[64];
		__pragma(warning(push)) __pragma(warning(disable : 6031))::StringFromGUID2(src, szGUID, 64);
		__pragma(warning(pop)) m_str = ::SysAllocString(szGUID);
	}
	WTComBSTR& operator=(const WTComBSTR& src)
	{
		if (m_str != src.m_str)
		{
			if (m_str)
				::SysFreeString(m_str);
			m_str = src.Copy();
		}
		return *this;
	}

	WTComBSTR& operator=(LPCOLESTR pSrc)
	{
		if (m_str)
			::SysFreeString(m_str);
		if (pSrc)
			m_str = ::SysAllocString(pSrc);
		else
			m_str = NULL;
		return *this;
	}

	~WTComBSTR()
	{
		::SysFreeString(m_str);
	}
	unsigned int Length() const
	{
		return (m_str == NULL) ? 0 : SysStringLen(m_str);
	}
	operator BSTR() const
	{
		return m_str;
	}
	BSTR* operator&()
	{
		return &m_str;
	}
	BSTR Copy() const
	{
		return ::SysAllocStringLen(m_str, ::SysStringLen(m_str));
	}
	HRESULT CopyTo(BSTR* pbstr)
	{
		ATLASSERT(pbstr != NULL);
		if (pbstr == NULL)
			return E_POINTER;
		*pbstr = ::SysAllocStringLen(m_str, ::SysStringLen(m_str));
		if (*pbstr == NULL)
			return E_OUTOFMEMORY;
		return S_OK;
	}
	void Attach(BSTR src)
	{
		ATLASSERT(m_str == NULL);
		m_str = src;
	}
	BSTR Detach()
	{
		BSTR s = m_str;
		m_str = NULL;
		return s;
	}
	void Empty()
	{
		::SysFreeString(m_str);
		m_str = NULL;
	}
	bool operator!() const
	{
		return (m_str == NULL);
	}
	HRESULT Append(const WTComBSTR& bstrSrc)
	{
		return Append(bstrSrc.m_str, SysStringLen(bstrSrc.m_str));
	}
	HRESULT Append(LPCOLESTR lpsz)
	{
		return Append(lpsz, (uint)ocslen(lpsz));
	}
	// a BSTR is just a LPCOLESTR so we need a special version to signify
	// that we are appending a BSTR
	HRESULT AppendBSTR(BSTR p)
	{
		return Append(p, SysStringLen(p));
	}
	HRESULT Append(LPCOLESTR lpsz, UINT nLen)
	{
		UINT n1 = Length();
		BSTR b;
		b = ::SysAllocStringLen(NULL, n1 + nLen);
		if (b == NULL)
			return E_OUTOFMEMORY;
		memcpy(b, m_str, n1 * sizeof(OLECHAR));
		memcpy(b + n1, lpsz, nLen * sizeof(OLECHAR));
		b[n1 + nLen] = NULL;
		SysFreeString(m_str);
		m_str = b;
		return S_OK;
	}

	WTComBSTR& operator+=(const WTComBSTR& bstrSrc)
	{
		AppendBSTR(bstrSrc.m_str);
		return *this;
	}
	bool operator<(BSTR bstrSrc) const
	{
		if (bstrSrc == NULL && m_str == NULL)
			return false;
		if (bstrSrc != NULL && m_str != NULL)
			return wcscmp(m_str, bstrSrc) < 0;
		return m_str == NULL;
	}
	bool operator==(BSTR bstrSrc) const
	{
		if (bstrSrc == NULL && m_str == NULL)
			return true;
		if (bstrSrc != NULL && m_str != NULL)
			return wcscmp(m_str, bstrSrc) == 0;
		return false;
	}
	bool operator<(LPCSTR pszSrc) const
	{
		if (pszSrc == NULL && m_str == NULL)
			return false;
		USES_CONVERSION;
		if (pszSrc != NULL && m_str != NULL)
			return wcscmp(m_str, A2W(pszSrc)) < 0;
		return m_str == NULL;
	}
	bool operator==(LPCSTR pszSrc) const
	{
		if (pszSrc == NULL && m_str == NULL)
			return true;
		USES_CONVERSION;
		if (pszSrc != NULL && m_str != NULL)
			return wcscmp(m_str, A2W(pszSrc)) == 0;
		return false;
	}
};
