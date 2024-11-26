#pragma once
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "EdCnt_fwd.h"
#include "WTString.h"

int CharIndexUTF8ToUTF16(const WTString& buf, int lineStart, int charIndex);

template <typename T>
void LC_RAD_2_VA(T* line, T* column) // convert Line/Column from RAD to VA
{
	if (column)
	{
		(*column) += 1;
	}
}

template <typename T>
void LC_RAD_2_VA(T* sline, T* scolumn, T* eline, T* ecolumn) // convert Line/Column from RAD to VA
{
	LC_RAD_2_VA(sline, scolumn);
	LC_RAD_2_VA(eline, ecolumn);
}

template <typename T>
void LC_VA_2_RAD(T* line, T* column) // convert Line/Column from VA to RAD
{
	if (line && *line == 0)
		*line = 1;

	if (column && *column)
	{
		(*column) -= 1;
	}
}

template <typename T>
void LC_VA_2_RAD(T* sline, T* scolumn, T* eline, T* ecolumn) // convert Line/Column from VA to RAD
{
	LC_VA_2_RAD(sline, scolumn);
	LC_VA_2_RAD(eline, ecolumn);
}

long LC_RAD_2_VA_POS(int line, int column); // convert Line/Column from RAD to VA LONG position

class RadDisableOptimalFill
{
	inline static int m_refs = 0;
	bool m_disabled = false;

  public:
	RadDisableOptimalFill(bool disableNow = true);
	~RadDisableOptimalFill();
	void Disable();
	void Restore();
};

class VaRSSelectionOverride
{
	EdCntPtr _ed;
	EdCntPtr _oldCur;

  public:
	VaRSSelectionOverride(const EdCntPtr& ed, long pos, bool updateScope = true);
	VaRSSelectionOverride(const EdCntPtr& ed, long start, long end, bool updateScope = true);

	~VaRSSelectionOverride();

	static bool GetSel(EdCnt* ed, long& start, long& end);
};

class RadWriteOperation
{
	CStringW _fileName;
	size_t _fileId;
	int lastLine = 0;
	int lastIndex = 0;

  public:
	RadWriteOperation(LPCWSTR fileName);
	~RadWriteOperation();

	const CStringW& GetFileName();
	bool IsFileName(LPCWSTR fileName);

	void DeleteTextVA(int startLine, int startCharIndex, int endLine, int endCharIndex);
	void InsertTextVA(int startLine, int startCharIndex, WTString text);
	void ReplaceTextVA(int startLine, int startCharIndex, int endLine, int endCharIndex, WTString text);

	void DeleteTextRAD(int startLine, int startCharIndex, int endLine, int endCharIndex);
	void InsertTextRAD(int startLine, int startCharIndex, WTString text);
	void ReplaceTextRAD(int startLine, int startCharIndex, int endLine, int endCharIndex, WTString text);
};

class RadUtils
{
  public:
	static bool TryGetFileContent(LPCWSTR fileName, WTString& outString);
	static bool SwapFileExtension(CStringW& file);;
};

struct RadStatus
{
	CStringW mToken;
	CStringW mTitle;
	CStringW mEndMessage;

	void PrintReport(LPCWSTR message, int percent = -1);

	RadStatus(LPCWSTR token, LPCWSTR title, LPCWSTR endMessage);
	~RadStatus();
};

#endif