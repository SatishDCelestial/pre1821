#include "stdafxed.h"
#if defined(RAD_STUDIO) || defined(VA_CPPUNIT)

#include "RadStudioUtils.h"
#include "EDCNT.H"
#include "EdCnt_fwd.h"
#include "StringUtils.h"
#include "CppBuilder.h"
#include "FILE.H"
#include "SubClassWnd.h"
#include "RadStudioPlugin.h"

std::map<EdCnt*, std::tuple<long, long>> sRadSelectionOverrides;

VaRSSelectionOverride::VaRSSelectionOverride(const EdCntPtr& ed, long pos, bool updateScope /*= true*/)
    : _ed(ed)
{
	sRadSelectionOverrides[ed.get()] = std::make_tuple(pos, pos);

	if (ed && updateScope)
	{
		ed->Scope(true);
		ed->CurScopeWord();

		_oldCur = g_currentEdCnt;

		if (ed != _oldCur)
			g_currentEdCnt = ed;
	}
}

VaRSSelectionOverride::VaRSSelectionOverride(const EdCntPtr& ed, long start, long end, bool updateScope /*= true*/)
    : _ed(ed)
{
	sRadSelectionOverrides[ed.get()] = std::make_tuple(start, end);

	if (ed && updateScope)
	{
		ed->Scope(true);
		ed->CurScopeWord();

		_oldCur = g_currentEdCnt;

		if (ed != _oldCur)
			g_currentEdCnt = ed;
	}
}

VaRSSelectionOverride::~VaRSSelectionOverride()
{
	sRadSelectionOverrides.erase(_ed.get());

	if (_ed && _ed == g_currentEdCnt &&
	    _oldCur && g_currentEdCnt != _oldCur)
	{
		g_currentEdCnt = _oldCur;
	}
}

bool VaRSSelectionOverride::GetSel(EdCnt* ed, long& start, long& end)
{
	auto found = sRadSelectionOverrides.find(ed);
	if (found != sRadSelectionOverrides.end())
	{
		start = std::get<0>(found->second);
		end = std::get<1>(found->second);
		return true;
	}
	return false;
}

int CharIndexUTF8ToUTF16(const WTString& buf, int lineStart, int charIndex)
{
	int p = lineStart;
	for (int diff = 1; buf[p]; p++, diff++)
	{
		if (p >= charIndex)
			return diff;

		if (buf[p] == '\n' || (buf[p] == '\r' && buf[p + 1] != '\n'))
			return -1;

		if (buf[p] & 0x80)
		{
			int len = ::GetUtf8SequenceLen(&buf.c_str()[p]);
			if (len)
			{
				if (4 == len)
				{
					// [case: 138734]
					// surrogate pair in utf16.
					// increase col count that we return, to return utf16
					// elements rather than chars.
					// this fixes selection of symbol during alt+g
					++diff;
				}

				p += len - 1;
			}
		}
	}
	return -1;
}

RadWriteOperation::RadWriteOperation(LPCWSTR fileName)
{
	_fileName = NormalizeFilepath(fileName);
	_fileId = gFileIdManager->GetFileId(_fileName);
	gRadStudioHost->BeginWrite(fileName);
}

void RadWriteOperation::DeleteTextVA(int startLine, int startCharIndex, int endLine, int endCharIndex)
{
	LC_VA_2_RAD(&startLine, &startCharIndex);
	LC_VA_2_RAD(&endLine, &endCharIndex);
	DeleteTextRAD(startLine, startCharIndex, endLine, endCharIndex);
}

void RadWriteOperation::InsertTextVA(int startLine, int startCharIndex, WTString text)
{
	LC_VA_2_RAD(&startLine, &startCharIndex);
	InsertTextRAD(startLine, startCharIndex, text);
}

void RadWriteOperation::ReplaceTextVA(int startLine, int startCharIndex, int endLine, int endCharIndex, WTString text)
{
	DeleteTextVA(startLine, startCharIndex, endLine, endCharIndex);
	InsertTextVA(startLine, startCharIndex, text);
}

void RadWriteOperation::DeleteTextRAD(int startLine, int startCharIndex, int endLine, int endCharIndex)
{
	lastLine = startLine;
	lastIndex = startCharIndex;
	gRadStudioHost->DeleteFromFile(startLine, startCharIndex, endLine, endCharIndex);
}

void RadWriteOperation::InsertTextRAD(int startLine, int startCharIndex, WTString text)
{
	lastLine = startLine;
	lastIndex = startCharIndex;
	gRadStudioHost->WriteToFile(startLine, startCharIndex, text.c_str());
}

void RadWriteOperation::ReplaceTextRAD(int startLine, int startCharIndex, int endLine, int endCharIndex, WTString text)
{
	DeleteTextRAD(startLine, startCharIndex, endLine, endCharIndex);
	InsertTextRAD(startLine, startCharIndex, text);
}

RadWriteOperation::~RadWriteOperation()
{
	gRadStudioHost->EndWrite();

	EdCntPtr ed = GetOpenEditWnd(_fileName);
	if (ed)
	{
		HWND ed_hWnd = ed->m_hWnd;
		RunFromMainThread([ed_hWnd]() {
			auto ed1 = GetOpenEditWnd(ed_hWnd);
			if (ed1)
				ed1->QueForReparse();
		}, false);
	}
}

const CStringW& RadWriteOperation::GetFileName()
{
	return _fileName;
}

bool RadWriteOperation::IsFileName(LPCWSTR fileName)
{
	CStringW normalized = NormalizeFilepath(fileName);
	return _fileName.CompareNoCase(normalized) == 0;
}


bool RadUtils::TryGetFileContent(LPCWSTR fileName, WTString & outString)
{
	outString.Empty();
	if (gRadStudioHost)
	{
		int length = gRadStudioHost->GetFileContentLength(fileName);
		if (length > 0)
		{
			std::vector<char> buffer;
			buffer.resize((size_t)length + 1);
			gRadStudioHost->GetFileContent(fileName, &buffer.front(), length + 1);
			outString = &buffer.front();
			return true;
		}
	}
	return false;
}

bool RadUtils::SwapFileExtension(CStringW& file)
{
	int pos = file.ReverseFind(L'.');
	if (-1 != pos)
	{
		LPCWSTR ext = (LPCWSTR)file + pos;

		// if file is right type then switch ext
		const WCHAR* const hdrExts[] = {L".h", L".hpp", L".hh", L".tlh", L".hxx", L".hp", L".rch", NULL};
		const WCHAR* const srcExts[] = {L".cpp", L".c", L".cc", L".tli", L".cxx", L".cp", L".rc", NULL};

		int idx = -1;
		bool isSrc = false;

		for (int i = 0; srcExts[i]; ++i)
		{
			if (_wcsicmp(ext, srcExts[i]) == 0)
			{
				idx = i;
				isSrc = true;
				break;
			}
		}

		if (idx == -1)
		{
			for (int i = 0; hdrExts[i]; ++i)
			{
				if (_wcsicmp(ext, hdrExts[i]) == 0)
				{
					idx = i;
					isSrc = false;
					break;
				}
			}
		}

		if (idx == -1)
			return false;

		CStringW fileNoExt = file.Left(pos);
		auto exts = isSrc ? hdrExts : srcExts;

		// first try same index
		CStringW currFile = fileNoExt + exts[idx];
		if (gRadStudioHost->IsFileInEditor(currFile))
		{
			file = currFile;
			return true;
		}

		for (int i = 0; exts[i]; ++i)
		{
			if (i == idx)
				continue;

			currFile = fileNoExt + exts[i];
			if (gRadStudioHost->IsFileInEditor(currFile))
			{
				file = currFile;
				return true;
			}
		}
	}
	return false;
}

void RadStatus::PrintReport(LPCWSTR message, int percent /*= -1*/)
{
	if (g_mainThread == ::GetCurrentThreadId())
	{
		if (gRadStudioHost)
		{
			gRadStudioHost->ShowStatusReport(mToken, message, percent);
		}
	}
	else
	{
		CStringW strMessage(message);
		CStringW strToken(mToken);
		RunFromMainThread([strToken, strMessage, percent]() {
			if (gRadStudioHost)
			{
				gRadStudioHost->ShowStatusReport(strToken, strMessage, percent);
			}
		}, false);
	}
}

RadStatus::RadStatus(LPCWSTR token, LPCWSTR title, LPCWSTR endMessage)
{
	mToken = token;
	mTitle = title;
	mEndMessage = endMessage;

	if (g_mainThread == ::GetCurrentThreadId())
	{
		if (gRadStudioHost)
		{
			gRadStudioHost->ShowStatusBegin(mToken, mTitle);
		}
	}
	else
	{
		CStringW strToken(mToken);
		CStringW strTitle(mTitle);
		RunFromMainThread([strToken, strTitle]() {
			if (gRadStudioHost)
			{
				gRadStudioHost->ShowStatusBegin(strToken, strTitle);
			}
		}, false);
	}
}

RadStatus::~RadStatus()
{
	if (g_mainThread == ::GetCurrentThreadId())
	{
		if (gRadStudioHost)
		{
			gRadStudioHost->ShowStatusEnd(mToken, mEndMessage);
		}
	}
	else
	{
		CStringW strToken(mToken);
		CStringW strMessage(mEndMessage);
		RunFromMainThread([strToken, strMessage]() {
			if (gRadStudioHost)
			{
				gRadStudioHost->ShowStatusEnd(strToken, strMessage);
			}
		}, false);
	}
}

RadDisableOptimalFill::RadDisableOptimalFill(bool disableNow /*= true*/)
{
	if (disableNow)
		Disable();
}

RadDisableOptimalFill::~RadDisableOptimalFill()
{
	Restore();
}

void RadDisableOptimalFill::Disable()
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());

	if (!m_disabled)
	{
		m_disabled = true;
		if (m_refs++ == 0)
		{
			if (gRadStudioHost)
				gRadStudioHost->DisableOptimalFill();
		}
	}
}

void RadDisableOptimalFill::Restore()
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());

	if (m_disabled)
	{
		m_disabled = false;
		
		_ASSERTE(m_refs != 0);
		
		if (m_refs > 0 && --m_refs == 0)
		{
			if (gRadStudioHost)
				gRadStudioHost->RestoreOptimalFill();
		}
	}
}

#endif
