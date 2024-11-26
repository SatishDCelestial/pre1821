#pragma once

#include "CFileW.h"

class WTString;

class WTFile : public CFileW
{
	DWORD state;

  public:
	WTFile();
	~WTFile();
	BOOL Open(const CStringW& file, UINT openFlags = CFile::modeRead | CFile::modeCreate | CFile::modeNoTruncate |
	                                                 CFile::shareDenyNone | CFile::typeBinary);
	void Write(LPCVOID lpBuffer, DWORD nCount);
	void Write(LPCSTR lpStr);
	void Flush();
	void Close();
};

class WTstream : public WTFile
{
  public:
	BOOL open(const CStringW& file, UINT iosMode);
	VS_STD::streampos tellg();
	VS_STD::streampos tellp()
	{
		return tellg();
	}
	BOOL good();
	BOOL fail()
	{
		return FALSE;
	}
	void close()
	{
		Close();
	}
	BOOL is_open()
	{
		return good();
	}
	BOOL eof()
	{
		return GetPosition() == GetLength();
	}
	void seekg(VS_STD::streampos pos, int iosFrom = 0);
	void seekp(VS_STD::streampos pos, int iosFrom = 0)
	{
		seekg(pos, iosFrom);
	}
	void clear()
	{
	}
	void write(LPCVOID data, UINT count)
	{
		Write(data, count);
	}
	int rdstate()
	{
		return 0;
	}
};

class WTofstream : public WTstream
{
  public:
	WTofstream(){};
	//	operator int(){ return good() == TRUE;}
	WTofstream(const CStringW& file, unsigned int iosMode = VS_STD::ios::out);
	~WTofstream();
	void flush()
	{
		Flush();
	}
	WTofstream& operator<<(const WTString& str);
	WTofstream& operator<<(LPCSTR str);
	WTofstream& operator<<(CHAR c)
	{
		Write(&c, 1);
		return *this;
	}
	WTofstream& operator<<(INT i);
	WTofstream& operator<<(UINT i);
};
