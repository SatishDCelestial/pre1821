#pragma once

class CFileW : public CFile
{
	CStringW mFilename;

  public:
	CFileW() : CFile()
	{
	}
	CFileW(HANDLE hFile) : CFile(hFile)
	{
	}
	//	CFileW(const CStringW& fileName, UINT nOpenFlags);

	virtual BOOL Open(const CStringW& fileName, UINT nOpenFlags);
	virtual void WriteUtf8Bom();
	virtual void Write(const void* lpBuf, UINT nCount);
	virtual void Flush();
	virtual void Close();
};
