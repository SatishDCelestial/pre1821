#include "stdafxed.h"
#include "wt_stdlib.h"
#include "WTString.h"
#include "timer.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

WTFile::WTFile() : CFileW(), state(NULL)
{
}

WTFile::~WTFile()
{
	DEFTIMER(WTofstream_Close);
	Close();
}

void WTFile::Write(LPCVOID lpBuffer, DWORD nCount)
{
	DEFTIMER(WTofstream_Write);
	state += nCount;
	__super::Write(lpBuffer, nCount);
}

void WTFile::Close()
{
	if (m_hFile != INVALID_HANDLE_VALUE)
		__super::Close();
}

BOOL WTFile::Open(const CStringW& file,
                  UINT openFlags /* = CFile::modeRead|CFile::modeCreate|CFile::modeNoTruncate|CFile::shareDenyNone */)
{
	DEFTIMER(WTofstream_Open);
	_ASSERTE(openFlags & CFile::shareDenyNone);
	BOOL retval = __super::Open(file, openFlags | CFile::modeNoInherit);
	SILENT_ASSERT1(m_hFile != INVALID_HANDLE_VALUE, "WTF::Open", CString(file));
	return retval;
}

void WTFile::Flush()
{
	if (state)
		__super::Flush();
	state = NULL;
}

void WTFile::Write(LPCSTR lpStr)
{
	Write((LPVOID)lpStr, strlen_u(lpStr));
}

WTofstream::WTofstream(const CStringW& file, uint iosMode)
{
	open(file, iosMode | VS_STD::ios::out);
}

WTofstream::~WTofstream()
{
}

WTofstream& WTofstream::operator<<(const WTString& str)
{
	Write((LPCVOID)str.c_str(), (DWORD)str.GetLength());
	return *this;
}

WTofstream& WTofstream::operator<<(LPCSTR str)
{
	Write(str);
	return *this;
}

void WTstream::seekg(VS_STD::streampos pos, int iosFrom)
{
	DEFTIMER(WTofstream_Seek);
	if (iosFrom == VS_STD::ios::end)
		Seek(pos, CFile::end);
	else
		Seek(pos, CFile::begin);
}

VS_STD::streampos WTstream::tellg()
{
	return (VS_STD::streampos)(LONGLONG)GetPosition();
}

BOOL WTstream::open(const CStringW& file, UINT iosMode)
{
	UINT mode = CFile::shareDenyNone | CFile::typeBinary;
	if (iosMode & VS_STD::ios::out)
		mode |= CFile::modeCreate | CFile::modeWrite;
	if (iosMode & VS_STD::ios::app)
		mode |= CFile::modeCreate | CFile::modeWrite | CFile::modeNoTruncate;
	if (iosMode & VS_STD::ios::in)
		mode |= CFile::modeRead;
#if _MSC_VER <= 1200
	if (iosMode & VS_STD::ios::nocreate)
		mode &= ~CFile::modeCreate;
#else
	if (iosMode & VS_STD::ios::_Nocreate)
		mode &= ~CFile::modeCreate;
#endif
	BOOL retval = Open(file, mode);
	if (retval && iosMode & VS_STD::ios::app)
		Seek(0, CFile::end);
	return retval;
}

BOOL WTstream::good()
{
	return m_hFile != INVALID_HANDLE_VALUE;
}

WTofstream& WTofstream::operator<<(INT i)
{
	Write(itos(i).c_str());
	return *this;
}

WTofstream& WTofstream::operator<<(UINT i)
{
	Write(utos(i).c_str());
	return *this;
}
