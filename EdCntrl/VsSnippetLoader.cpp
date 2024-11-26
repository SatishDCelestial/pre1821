#include "stdafxed.h"
#include <shlobj.h>
#include "VsSnippetLoader.h"
#include <xmllite.h>
#include "FileTypes.h"
#include "Registry.h"
#include "VaService.h"
#include "vsshell90.h"
#include "FILE.H"
#include "TokenW.h"
#include "ShellListener.h"
#include "DevShellService.h"
#include "PROJECT.H"

#pragma comment(lib, "xmllite.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// see also MPF example:
// Walkthrough: Getting a List of Installed Code Snippets (Managed Package Framework)
// https://msdn.microsoft.com/en-us/library/bb165947.aspx

static void FindSnippetFiles(const CStringW& curPath, WideStrVector& snippetFiles);

class SnippetFileParser
{
  public:
	SnippetFileParser(const CStringW& snipFile, Snippets& snips)
	    : mSnippets(snips), mCurPathState(sepNone), mCurItemIsExpansionSnippet(false)
	{
		if (!Init(snipFile))
			return;

		Read();
	}

  private:
	BOOL Init(const CStringW& snipFile)
	{
		// http://msdn.microsoft.com/en-us/magazine/cc163436.aspx
		HRESULT res = ::SHCreateStreamOnFileW(snipFile, STGM_READ, &mFileStream);
		if (S_OK != res)
			return FALSE;

		res = ::CreateXmlReader(__uuidof(IXmlReader), (void**)&mReader, nullptr);
		if (S_OK != res)
			return FALSE;

		res = mReader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
		if (S_OK != res)
			return FALSE;

		res = mReader->SetInput(mFileStream);
		if (S_OK != res)
			return FALSE;

		return TRUE;
	}

	void Read()
	{
		// http://msdn.microsoft.com/en-us/library/windows/desktop/ms752838%28v=vs.85%29.aspx
		HRESULT hr;
		XmlNodeType nodeType;
		const WCHAR* pwszPrefix;
		const WCHAR* pwszLocalName;
		const WCHAR* pwszValue;
		UINT cwchPrefix;

		while (S_OK == (hr = mReader->Read(&nodeType)))
		{
			switch (nodeType)
			{
			case XmlNodeType_Element:
				if (FAILED(hr = mReader->GetPrefix(&pwszPrefix, &cwchPrefix)))
					break;
				if (FAILED(hr = mReader->GetLocalName(&pwszLocalName, nullptr)))
					break;

				if (cwchPrefix > 0)
					CString__FormatW(mCurrentElementName, L"%s:%s", pwszPrefix, pwszLocalName);
				else
					mCurrentElementName = pwszLocalName;

				OnElementBegin();
				break;

			case XmlNodeType_Text:
				if (FAILED(hr = mReader->GetValue(&pwszValue, nullptr)))
					break;

				OnTextValue(pwszValue);
				break;

			case XmlNodeType_EndElement:
				if (FAILED(hr = mReader->GetPrefix(&pwszPrefix, &cwchPrefix)))
					break;
				if (FAILED(hr = mReader->GetLocalName(&pwszLocalName, nullptr)))
					break;

				if (cwchPrefix > 0)
					CString__FormatW(mCurrentElementName, L"%s:%s", pwszPrefix, pwszLocalName);
				else
					mCurrentElementName = pwszLocalName;

				OnElementEnd();
				break;

			default:
				break;
			}
		}
	}

	void OnElementBegin()
	{
		switch (mCurPathState)
		{
		case sepNone:
			if (mCurrentElementName == L"CodeSnippets")
				mCurPathState = sepCodeSnippets;
			break;
		case sepCodeSnippets:
			if (mCurrentElementName == L"CodeSnippet")
			{
				// new snippet, reset
				mCurPathState = sepCodeSnippet;
				ResetItem();
			}
			break;
		case sepCodeSnippet:
			if (mCurrentElementName == L"Header")
				mCurPathState = sepHeader;
			break;
		case sepHeader:
			if (mCurrentElementName == L"Title")
				mCurPathState = sepTitle;
			else if (mCurrentElementName == L"Shortcut")
				mCurPathState = sepShortcut;
			else if (mCurrentElementName == L"Description")
				mCurPathState = sepDesc;
			else if (mCurrentElementName == L"SnippetTypes")
				mCurPathState = sepSnippetTypes;
			break;
		case sepSnippetTypes:
			if (mCurrentElementName == L"SnippetType")
				mCurPathState = sepSnippetType;
			break;

		default:
			break;
		}
	}

	void OnTextValue(const WCHAR* pwszValue)
	{
		switch (mCurPathState)
		{
		case sepTitle:
			mCurItemTitle = pwszValue;
			break;
		case sepShortcut:
			mCurItemShortcut = pwszValue;
			break;
		case sepDesc:
			mCurItemDesc = pwszValue;
			break;
		case sepSnippetType: {
			const CStringW t(pwszValue);
			if (t == L"Expansion")
				mCurItemIsExpansionSnippet = true;
		}
		break;

		default:
			break;
		}
	}

	void OnElementEnd()
	{
		switch (mCurPathState)
		{
		case sepSnippetType:
			if (mCurrentElementName == L"SnippetType")
				mCurPathState = sepSnippetTypes;
			break;
		case sepSnippetTypes:
			if (mCurrentElementName == L"SnippetTypes")
				mCurPathState = sepHeader;
			break;
		case sepTitle:
			if (mCurrentElementName == L"Title")
				mCurPathState = sepHeader;
			break;
		case sepShortcut:
			if (mCurrentElementName == L"Shortcut")
				mCurPathState = sepHeader;
			break;
		case sepDesc:
			if (mCurrentElementName == L"Description")
				mCurPathState = sepHeader;
			break;
		case sepHeader:
			if (mCurrentElementName == L"Header")
				mCurPathState = sepCodeSnippet;
			break;
		case sepCodeSnippet:
			if (mCurrentElementName == L"CodeSnippet")
			{
				mCurPathState = sepCodeSnippets;
				SaveItem();
			}
			break;
		case sepCodeSnippets:
			if (mCurrentElementName == L"CodeSnippets")
				mCurPathState = sepNone;
			break;

		default:
			break;
		}
	}

	void ResetItem()
	{
		mCurItemIsExpansionSnippet = false;
		mCurItemTitle.Empty();
		mCurItemShortcut.Empty();
		mCurItemDesc.Empty();
	}

	void SaveItem()
	{
		if (!mCurItemIsExpansionSnippet)
			return;

		if (mCurItemTitle.IsEmpty() && mCurItemShortcut.IsEmpty())
			return;

		mSnippets.emplace_back(WTString(mCurItemTitle), WTString(mCurItemShortcut), WTString(mCurItemDesc));
	}

  private:
	CComPtr<IStream> mFileStream;
	CComPtr<IXmlReader> mReader;
	Snippets& mSnippets;
	CStringW mCurrentElementName;
	enum SnippetElementPath
	{
		sepNone,
		sepCodeSnippets,
		sepCodeSnippet,
		sepHeader,
		sepTitle,
		sepShortcut,
		sepDesc,
		sepSnippetTypes,
		sepSnippetType
	};
	SnippetElementPath mCurPathState;
	bool mCurItemIsExpansionSnippet;
	CStringW mCurItemTitle, mCurItemShortcut, mCurItemDesc;
};

VsSnippetLoader::VsSnippetLoader(SnippetMapPtr saveTo)
    : PooledThreadBase("VsSnippetLoader"), mDestinationSnippets(saveTo)
{
	{
		// this has to run on the UI thread;
		// on loader thread, the GetProperty call returns success but val is empty
		CComVariant val;
		CComPtr<IVsShell> vsh(::GetVsShell());
		if (vsh && S_OK == vsh->GetProperty(VSSPROPID_InstallRootDir, &val))
			mRootInstallDir = val.bstrVal;
	}

	StartThread();
}

void VsSnippetLoader::Run()
{
	while (!gDte && !gShellIsUnloading)
		Sleep(5000);

	if (gShellIsUnloading)
		return;

		// for now, only C/C++ is supported
#ifdef AVR_STUDIO
	LoadLanguageSnippets(Src, "GCC", "GCC");
#else
	LoadLanguageSnippets(Src, "C/C++", "Microsoft Visual C++");
#endif
	mDestinationSnippets->swap(mSnippets);
}

void VsSnippetLoader::LoadLanguageSnippets(int lang, LPCSTR regLangSubstr, LPCSTR valueName)
{
	Snippets langSnippets;
	CStringW paths(RegGetSnippetPaths(regLangSubstr, valueName));
	vCatLog("Environment.Directories", "SnippetDirs for %s: %s", regLangSubstr, (LPCTSTR)CString(paths));
	TokenW t(paths);
	while (t.more())
	{
		CStringW curPath(t.read(L";"));
		WideStrVector snippetFiles;
		::FindSnippetFiles(curPath, snippetFiles);
		std::for_each(snippetFiles.begin(), snippetFiles.end(),
		              [&langSnippets](const CStringW& snipFile) { SnippetFileParser p(snipFile, langSnippets); });
	}

	if (langSnippets.size())
		mSnippets[lang] = langSnippets;
}

#if 0
void 
DumpDtePropertyNames(CComPtr<EnvDTE::Properties> pProperties)
{
	_ASSERTE(g_mainThread == GetCurrentThreadId());
	CComPtr<IUnknown> pUnk;
	HRESULT hres = pProperties->_NewEnum(&pUnk);
	if (SUCCEEDED(hres) && pUnk != nullptr)
	{
		CComQIPtr<IEnumVARIANT, &IID_IEnumVARIANT> pNewEnum(pUnk);
		if (pNewEnum)
		{
			VARIANT varItem;
			VariantInit(&varItem);
			CComQIPtr<EnvDTE::Property> pItem;
			ULONG enumVarCnt = 0;
			while (pNewEnum->Next(1, &varItem, &enumVarCnt) == S_OK)
			{
				_ASSERTE(varItem.vt == VT_DISPATCH || varItem.vt == VT_UNKNOWN);
				pItem = varItem.pdispVal;
				VariantClear(&varItem);
				if (!pItem)
					continue;

				CComBSTR nm;
				if (SUCCEEDED(pItem->get_Name(&nm)))
				{
					CStringW msg;
					msg.Format(L"PropNameDump: %s\r\n", nm);
					OutputDebugStringW(msg);
				}
			}
		}
	}
}
#endif

CStringW VsSnippetLoader::RegGetSnippetPaths(LPCSTR regLangSubstr, LPCSTR valueName) const
{
	// HKEY_CURRENT_USER\Software\Microsoft\VisualStudio\11.0_Config\Languages\CodeExpansions\C/C++\Paths
	// value name: "Microsoft Visual C++"
	// value: C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\Snippets\%LCID%\Visual C++\;%MyDocs%\Code
	// Snippets\Visual C++\My Code Snippets\ 

	CString keyName, keyBase;

	CComBSTR regRootBstr;
	_ASSERTE(gDte);
	gDte->get_RegistryRoot(&regRootBstr);
	keyBase = regRootBstr;
	if (keyBase.IsEmpty())
	{
		vLog("ERROR: VS Snippet path fail");
		return CStringW();
	}

	CString__FormatA(keyName, "%s_Config\\Languages\\CodeExpansions\\%s\\Paths", (LPCTSTR)keyBase, regLangSubstr);
	CStringW paths(GetRegValueW(HKEY_CURRENT_USER, keyName, valueName));
	vCatLog("Environment.Directories", "Snippet paths: %s", (LPCTSTR)CString(paths));
	if (-1 != paths.Find(L"%InstallRoot%"))
	{
		// replace "%InstallRoot%" with "c:\\Program Files\\..."
		paths.Replace(L"%InstallRoot%", mRootInstallDir);
		vCatLog("Environment.Directories", "Snippet paths updated: %s", (LPCTSTR)CString(paths));
	}

	if (-1 != paths.Find(L"%MyDocs%"))
	{
		CStringW myDocs;
		CComVariant val;
		CComPtr<IVsShell> vsh(::GetVsShell());
		// 	http://msdn.microsoft.com/en-us/library/microsoft.visualstudio.shell.interop.__vsspropid2.aspx
		if (vsh && S_OK == vsh->GetProperty(VSSPROPID_VisualStudioDir, &val))
		{
			myDocs = val.bstrVal;
		}
		else if (gShellSvc)
		{
			WCHAR docs[MAX_PATH * 2];
			if (S_OK == SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, docs))
			{
				myDocs = docs;
				myDocs += CStringW(L"\\") + gShellSvc->GetMyDocumentsProductDirectoryName();
			}
		}

		// replace "%MyDocs%" with "User Documents\\Visual Studio 2012\\"
		paths.Replace(L"%MyDocs%", myDocs);
	}

	if (gShellAttr && gShellAttr->IsDevenv15OrHigher())
	{
		CString cpp("C/C++");
		if (cpp == regLangSubstr)
		{
			// [case: 99719]
			// hack workaround for dev15p4 -- check for default installation
			CStringW altSnippetDir(mRootInstallDir);
			altSnippetDir += L"Common7\\IDE\\VC\\Snippets\\%LCID%\\Visual C++\\;";
			paths += ";";
			if (-1 == paths.Find(altSnippetDir))
			{
				paths += altSnippetDir;
				vCatLog("Environment.Directories", "Snippet paths updated: %s", (LPCTSTR)CString(paths));
			}
		}
	}

	const LCID userLcid = GetUserDefaultLCID();
	CStringW userLcidStr;
	CString__FormatW(userLcidStr, L"%ld", static_cast<DWORD>(userLcid));

	// for each dir, replace %LCID%
	// iterate over paths
	TokenW t(paths);
	paths.Empty();
	while (t.more())
	{
		CStringW curPath(t.read(L";"));
		if (curPath.IsEmpty())
			continue;

		if (-1 != curPath.Find(L"%LCID%"))
		{
			curPath.Replace(L"%LCID%", userLcidStr);
			// confirm dir exists, else default to 1033
			if (!IsDir(curPath) && userLcid != 1033)
				curPath.Replace(userLcidStr, L"1033");
		}

		if (curPath[curPath.GetLength() - 1] != L'\\' && curPath[curPath.GetLength() - 1] != L'/')
			curPath += L"\\";

		if (!IsDir(curPath))
			continue;

		curPath += L";";
		if (-1 == paths.Find(curPath))
			paths += curPath;
	}

	vCatLog("Environment.Directories", "Snippet paths final: %s", (LPCTSTR)CString(paths));
	return paths;
}

void FindSnippetFiles(const CStringW& curPath, WideStrVector& snippetFiles)
{
	CStringW searchSpec;
	CString__FormatW(searchSpec, L"%s*.snippet", (LPCWSTR)curPath);
	WIN32_FIND_DATAW fileData;
	HANDLE hFile;
	hFile = FindFirstFileW(searchSpec, &fileData);
	if (INVALID_HANDLE_VALUE == hFile)
		return;

	do
	{
		snippetFiles.push_back(curPath + fileData.cFileName);
	} while (FindNextFileW(hFile, &fileData));
	FindClose(hFile);
}
