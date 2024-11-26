#include "StdAfxEd.h"
#include "GetFileText.h"
#include "VaService.h"
#include "Edcnt.h"
#include "mainThread.h"
#include "StringUtils.h"
#include "FILE.H"
#include "vsshell.h"
#include "CodeGraph.h"
#include "..\..\..\..\12.0\VisualStudioIntegration\Common\Inc\vsshell120.h"
#include "LOG.H"

#ifdef RAD_STUDIO
#include "RadStudioPlugin.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif // _DEBUG

static CComPtr<IUnknown> RdtLookupDocument(const CStringW& filename)
{
	CComPtr<IUnknown> docUnk;
	if (!gVsRunningDocumentTable)
		return docUnk;

	CComPtr<IEnumRunningDocuments> docsEnum;
	HRESULT hr = gVsRunningDocumentTable->GetRunningDocumentsEnum(&docsEnum);
	if (!(SUCCEEDED(hr) && docsEnum))
		return docUnk;

	CComBSTR lowerName(filename);
	lowerName.ToLower();

	docsEnum->Reset();

	VSCOOKIE curItem;
	while (SUCCEEDED(docsEnum->Next(1, &curItem, NULL)))
	{
		VSRDTFLAGS rdtFlags = 0;
		CComBSTR docName;
		hr = gVsRunningDocumentTable->GetDocumentInfo(curItem, &rdtFlags, NULL, NULL, &docName, NULL, NULL, NULL);
		if (!SUCCEEDED(hr))
			break;

		if (rdtFlags & RDT_ProjSlnDocument)
			continue;

		if (rdtFlags & RDT_PendingInitialization)
		{
			// [case: 89231]
			// per documentation at
			// https://msdn.microsoft.com/en-us/library/microsoft.visualstudio.shell.interop.ivsrunningdocumenttable4.getdocumenthierarchyitem.aspx
			// a document that has RDT_PendingInitialization hasn't been loaded, and
			// the view hasn’t been created; don't force it to load and potentially
			// cause a deadlock with project background thread loader
			continue;
		}

		docName.ToLower();
		if (docName == lowerName)
		{
			CComPtr<IUnknown> curdocUnk;
			hr = gVsRunningDocumentTable->GetDocumentInfo(curItem, NULL, NULL, NULL, NULL, NULL, NULL, &curdocUnk);
			if (!SUCCEEDED(hr))
				break;

			return curdocUnk;
		}
	}

	return docUnk;
}

// Returns max char count for the buffer.
// This assumes 2 chars per linebreak - actual
// text may only use one.
long GetTextBufferCharCount(IVsTextBuffer* buf)
{
	long len = 0;
	long lineCnt = 0;
	HRESULT hr = buf->GetLineCount(&lineCnt);
	for (long idx = 0; idx < lineCnt; ++idx)
	{
		long lineLen = 0;
		hr = buf->GetLengthOfLine(idx, &lineLen);
		if (!(SUCCEEDED(hr)))
			return 0;
		len += lineLen + 2;
	}
	return len;
}

#if !defined(RAD_STUDIO)
static CStringW PkgGetFileTextWImpl(IUnknown* linesUnk, IUnknown* docUnk)
{
	HRESULT hr;
	CStringW txtW;

#ifdef _WIN64
	CComQIPtr<IVsTextLines> textLines(linesUnk);
	if (textLines)
	{
		long lines = 0;
		if (textLines->GetLineCount(&lines) == S_OK && lines != 0)
		{
			long lastLineLength = 0;
			if (textLines->GetLengthOfLine(lines - 1, &lastLineLength) == S_OK)
			{
				BSTR str = nullptr;
				if (textLines->GetLineText(0, 0, lines - 1, lastLineLength, &str) == S_OK && str)
				{
					txtW = str;
					SysFreeString(str);
				}
			}
		}
	}
#else
	CComQIPtr<IVsFullTextScanner> scanner(linesUnk);
	if (scanner)
	{
		hr = scanner->OpenFullTextScan();
		if (SUCCEEDED(hr))
		{
			long writtenLen;
			const WCHAR* tempBuf = nullptr;
			hr = scanner->FullTextRead(&tempBuf, &writtenLen);
			if (SUCCEEDED(hr) && tempBuf)
			{
				try
				{
					txtW.SetString(tempBuf, writtenLen);
				}
				catch (CException* e)
				{
					// [case: 110699]
					TCHAR msg[256];
					e->GetErrorMessage(msg, 255);
					vLog("ERROR: SetString exception caught in PkgGetFileText: %s (%ld)", msg, writtenLen);
					e->Delete();
					txtW.Empty();
				}
			}

			// this call releases the memory that FullTextRead allocated for tempBuf
			scanner->CloseFullTextScan();
		}
	}
#endif

	if (txtW.IsEmpty())
	{
		CComQIPtr<IVsTextStream> strm(docUnk);
		if (strm)
		{
			CComQIPtr<IVsTextBuffer> buf(docUnk);
			if (buf)
			{
				const long len = GetTextBufferCharCount(buf);
				if (len)
				{
					LPWSTR p = txtW.GetBufferSetLength(len + 1);
					if (p)
					{
						hr = strm->GetStream(0, len, p);
						if (SUCCEEDED(hr))
							txtW.ReleaseBuffer(len);
						else
							txtW.ReleaseBuffer(0);
					}
				}
			}
		}
	}

	return txtW;
}
#endif

CStringW PkgGetFileTextW(const CStringW filename)
{
	CStringW txt;
#if defined(RAD_STUDIO)
	_ASSERTE(gVaRadStudioPlugin);

	WTString wtStr;
	if (RadUtils::TryGetFileContent(filename, wtStr))
		txt = wtStr.Wide();
	else if (gVaRadStudioPlugin)
		txt = gVaRadStudioPlugin->GetRunningDocText(filename).Wide();
	
	if (txt.GetLength())
		return txt;
#else
	CComPtr<IUnknown> docUnk(RdtLookupDocument(filename));
	if (docUnk)
		txt = PkgGetFileTextWImpl(docUnk, docUnk);
#endif

	if (txt.IsEmpty())
		ReadFileW(filename, txt);

	return txt;
}

CStringW PkgGetFileTextW(IVsTextView* edVsTextView)
{
#if defined(RAD_STUDIO)
	return CStringW();
#else
	CComPtr<IUnknown> docUnk(edVsTextView);
	if (!docUnk)
		return CStringW();

	CComPtr<IVsTextLines> lines;
	edVsTextView->GetBuffer(&lines);
	return PkgGetFileTextWImpl(lines, docUnk);
#endif
}

WTString GetFileText(const CStringW& file)
{
	if (g_CodeGraphWithOutVA || (gVaShellService && g_mainThread == ::GetCurrentThreadId()) || (gShellAttr && gShellAttr->IsCppBuilder()))
	{
		WTString txt(PkgGetFileTextW(file));
		if (txt.GetLength())
			return txt;
	}

	EdCntPtr ed = GetOpenEditWnd(file);
	if (ed)
	{
		WTString edBuf(ed->GetBuf());
#ifdef RAD_STUDIO
		RemovePadding_kExtraBlankLines(edBuf);
#endif
		if (edBuf.GetLength())
			return edBuf;
	}

	WTString buf;
	buf.ReadFile(file);
	return buf;
}

CStringW GetFileTextW(const CStringW& file)
{
	CStringW txt;
	if (g_CodeGraphWithOutVA || (gVaShellService && g_mainThread == ::GetCurrentThreadId()) || (gShellAttr && gShellAttr->IsCppBuilder()))
	{
		txt = PkgGetFileTextW(file);
		if (txt.GetLength())
			return txt;
	}

	EdCntPtr ed = GetOpenEditWnd(file);
	if (ed)
	{
		const WTString edBuf(ed->GetBuf());
		if (edBuf.GetLength())
		{
			txt = edBuf.Wide();
			return txt;
		}
	}

	ReadFileW(file, txt);
	return txt;
}

FileModificationState PkgGetFileModState(const CStringW& filename)
{
	CComPtr<IUnknown> docUnk(RdtLookupDocument(filename));
	if (!docUnk)
		return fm_notFound;

	CComQIPtr<IVsPersistDocData2> docPersist(docUnk);
	if (!docPersist)
		return fm_notFound;

	BOOL isDirty;
	HRESULT hr = docPersist->IsDocDataDirty(&isDirty);
	if (!(SUCCEEDED(hr)))
		return fm_notFound;

	return isDirty ? fm_dirty : fm_clean;
}
