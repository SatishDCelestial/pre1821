#include "stdafxed.h"
#include "WTString.h"
#include "log.h"
#include "DbFieldConstants.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

////////////////////////////////////
// Sorting
int qsortfun2(const void* a, const void* b)
{
	LPCTSTR strA = ((char**)a)[0];
	LPCTSTR strB = ((char**)b)[0];
	// compare by symbol only - field delimiter causes sorting problems
	//  since it is greater than any alpha letter
	//  prevent cstringarray ending up less than cstring - s/b cstring, cstringarray
	char* posA = (LPSTR)strchr(strA, DB_FIELD_DELIMITER);
	char* posB = (LPSTR)strchr(strB, DB_FIELD_DELIMITER);
	if (posA)
		*posA = '\0';
	if (posB)
		*posB = '\0';
	int retval = _tcsicmp(strA, strB);
	if (posA)
		*posA = DB_FIELD_DELIMITER;
	if (posB)
		*posB = DB_FIELD_DELIMITER;
	if (!retval) // if symbols are identical, then compare on entire record
		return _tcsicmp(strA, strB);
	return retval;
}

void QSort(const CStringW& fin)
{
	const int kBufLen = 4096 + 500;
	const std::unique_ptr<char[]> bufVec(new char[kBufLen + 2]);
	if (!bufVec)
	{
		vLog("ERROR: qsort alloc fail A");
		return;
	}

	char* buf = &bufVec[0];
	const int kArrSz = 600000;
	char** data = new char*[kArrSz + 1];
	if (!data)
	{
		vLog("ERROR: qsort alloc fail B");
		return;
	}

	ZeroMemory(data, (kArrSz + 1) * sizeof(char*));
	uint n = 0;
	WTString fBuf;
	if (!fBuf.ReadFile(fin, -1, true))
	{
		vLog("ERROR: qsort read fail %s", (const char*)CString(fin));
		delete[] data;
		return;
	}

	LONG fLen = fBuf.GetLength();
	LPTSTR pBegin = fBuf.GetBuffer(0), pEnd;
	const LPCTSTR pStop = pBegin + fLen;

	for (; pBegin < pStop && *pBegin;)
	{
		if (n >= kArrSz)
		{
			Log("ERROR: QSort failure - out of space");
			_ASSERTE(!"QSort failure - out of space");
			delete[] data;
			fBuf.ReleaseBuffer(0);
			fBuf.Empty();
			return;
		}
		pEnd = (LPTSTR)strchr(pBegin, '\n');
		if (!pEnd || pEnd >= pStop) // normal break condition
			break;
		if (pEnd == pBegin)
		{
			pBegin++;
			data[n] = NULL;
			continue;
		}

		*pEnd = '\0';
		data[n] = pBegin;
		pBegin = pEnd + 1;
		n++;
	}

	qsort((void*)data, n, sizeof(data[0]), qsortfun2);

	CFileW ofs;
	ofs.Open(fin, CFile::typeBinary | CFile::modeCreate | CFile::modeWrite);
	for (uint i = 0; i < n; i++)
	{
		if (data[i] && (!data[i + 1] || strcmp(data[i], data[i + 1])))
		{
			size_t symLen = strlen(data[i]);
			if (symLen && symLen < kBufLen)
			{
				strcpy(buf, data[i]);
				if (buf[symLen - 1] != '\n')
					buf[symLen++] = '\n';
				buf[symLen] = '\0';
				_ASSERTE(symLen == strlen(buf));
				ofs.Write(buf, (uint)symLen);
			}
			else
				_ASSERTE(symLen < kBufLen);
		}
	}

	fBuf.ReleaseBuffer(0);
	delete[] data;
}
