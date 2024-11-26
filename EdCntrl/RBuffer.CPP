#include "stdafxed.h"
#include "resource.h"
#include "wtstring.h"
#include "file.h"
#include "rbuffer.h"
#include "token.h"
#include "Directories.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MSC_VER > 1200
using std::filebuf;
using std::ios;
#endif

const CStringW kTypeBufferDirName("typeHist");
#define kTypeBufferPath VaDirs::GetDbDir() + kTypeBufferDirName
#define kTypeBufferSortDir CStringW(kTypeBufferPath + L"\\sort")
#define TYPINGBUFFERFILE CStringW(kTypeBufferPath + L"\\buf.txt")
#define TYPINGBUFFERSIZE 10000

static ExpHistory s_EcpHistory;
ExpHistory* g_ExpHistory = &s_EcpHistory;

WTString ExpHistory::GetExpHistory(const WTString& startswith)
{
	if (startswith.IsEmpty())
		return WTString("");
	CStringW file = kTypeBufferSortDir + L"\\_" + startswith[0] + L".txt";
	AutoLockCs l(mLock);
	if (IsFile(file))
		return ReadFile(file);
	return WTString("");
}

void ExpHistory::AddExpHistory(const WTString& sym)
{
	if (sym.IsEmpty() || !sym[0] || !sym[1] || !ISCSYM(sym[0]) || !ISCSYM(sym[1]))
		return;

	g_rbuffer.Add(sym); // Also add it to rbuffer

	CStringW sWith;
	if (sym[0] & 0x80)
	{
		CStringW tmp(sym.Wide());
		sWith = tmp[0];
	}
	else
		sWith = sym[0];
	sWith.MakeLower();

	const CStringW file = kTypeBufferSortDir + L"\\_" + sWith + L".txt";
	WTString txt;
	AutoLockCs l(mLock);
	if (IsFile(file))
		txt = SPACESTR + sym + ReadFile(file);
	else
		txt = SPACESTR + sym + SPACESTR;

	if (txt.GetLength() > 500)
		txt = txt.Mid(0, 500);

	WTofstream tfile;
#if _MSC_VER <= 1200
	tfile.open(file, ios::out | ios::out | ios::binary, filebuf::openprot | filebuf::sh_read | filebuf::sh_write);
#else
	// filebuf protections not supported in vs2003?
	tfile.open(file, ios::out | ios::out | ios::binary);
#endif

	tfile << txt.c_str();
	tfile.close();
}

void ExpHistory::Clear()
{
	if (::IsDir(kTypeBufferSortDir))
	{
		AutoLockCs l(mLock);
		::CleanDir(kTypeBufferSortDir, L"*.*");
	}
}

void CRollingBuffer::Init()
{
	if (!::IsDir(kTypeBufferSortDir))
	{
		::CreateDir(kTypeBufferPath);
		::CreateDir(kTypeBufferSortDir);
	}

	AutoLockCs l(mLock);
	if (::IsFile(TYPINGBUFFERFILE))
		m_buf = ::ReadFile(TYPINGBUFFERFILE);
}

void CRollingBuffer::Save()
{
	AutoLockCs l(mLock);
	WTofstream ofs(TYPINGBUFFERFILE, VS_STD::ios::out | VS_STD::ios::binary);
	if (!ofs.good())
		return;

	ofs << m_buf.c_str();
}

void CRollingBuffer::Add(const WTString& str)
{
	const WTString strToAdd(str + " ");
	AutoLockCs l(mLock);
	m_buf += strToAdd;
	if (m_buf.GetLength() > TYPINGBUFFERSIZE)
	{
		int i;
		for (i = TYPINGBUFFERSIZE * 2 / 3; i < TYPINGBUFFERSIZE && !ISCSYM(m_buf[i]); i++)
			;
		m_buf = m_buf.Mid(i);
	}
}

void CRollingBuffer::Clear()
{
	AutoLockCs l(mLock);
	m_buf.Empty();

	if (::IsDir(kTypeBufferPath))
		::CleanDir(kTypeBufferPath, L"*.*");
}
