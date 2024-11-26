#include "stdafxed.h"
#include "project.h"
#include "VaMessages.h"
#include "FOO.H"
#include "FILE.H"
#include "CodeGraph.h"
#include "Mparse.h"
#include "FDictionary.h"
#include "DBQuery.h"
#include "VARefactor.h"
#include "VAParseCodeGraph.h"
#include "RenameReferencesDlg.h"
#include "VAAutomation.h"
#include "ParseThrd.h"
#include "BuildInfo.h"
#include "..\common\ThreadStatic.h"
#include "GetFileText.h"
#include "StringUtils.h"
#include "DTypeDbScope.h"
#include "FileId.h"
#include "VaAddinClient.h"
#include "Registry.h"
#include "RegKeys.h"

BOOL g_CodeGraphWithOutVA = FALSE;

namespace CodeGraphNS
{
BOOL g_includeGlobalRefs = false;

CStringW GetDbGraphDir()
{
	CStringW projectDir = GlobalProject->GetProjDbDir();
	if (projectDir.GetLength() == 0)
		projectDir = VaDirs::GetDbDir();
	CStringW graphDir = projectDir + L"RefDB9"; // Incremented with each change to DB or so it reparses
	return graphDir;
}

uint CGHashKey(LPCSTR symscope)
{
	if (symscope && symscope[0] != ':')
		return gFileIdManager->GetFileId(CStringW(symscope));
	return WTHashKey(symscope);
}

uint CGHashKey(const WTString& symscope)
{
	return CGHashKey(symscope.c_str());
}

bool IsValidSym(DType* refScope)
{
	if (!refScope || !refScope->FileId())
		return false;
	if ((gTypingDevLang == CS || gTypingDevLang == VB) && refScope->ScopeHash() == 0 &&
	    refScope->MaskedType() != NAMESPACE && !refScope->IsType())
		return false; // No global symbols in managed code
	if (!g_includeGlobalRefs && refScope->IsSystemSymbol())
		return false;
	if (IsCFile(gTypingDevLang) && refScope->ScopeHash() == 0 && StrIsUpperCase(refScope->Sym()))
	{
		switch (refScope->MaskedType())
		{
		case CLASS:
		case STRUCT:
		case NAMESPACE:
			break;
		default:
			return false; // Filter bogus macros since system include paths are not parsed.
		}
	}
	if (gTypingDevLang == CS && refScope->Sym() == "base")
		return false;
	WTString refScopeDef(refScope->Def());
	if (refScopeDef == refScope->SymScope())
		return false; // CS:  List<int> m_member; gets recorded
	if (!refScope->ScopeHash() && !(refScope->IsType() || refScope->IsMethod()))
		return false; // Only include Global methods and types, no vars or macro's
	if (refScope->HasLocalScope())
		return false; // No local vars
	if (refScopeDef == " ")
		return false; // No hidden symbols
	if (refScope->MaskedType() == TEMPLATETYPE)
		return false; // < >
	return true;
}

class VAFileRefrences : public IReferenceNodesContainer
{
  public:
	VAFileRefrences(LPCWSTR file)
	{
		mFile = file;
		mFileId = gFileIdManager->GetFileId(file);
		CStringW graphDir = GetDbGraphDir();
		UINT fileId = gFileIdManager->GetFileId(mFile);
		CString__FormatW(m_BaseFile, L"%s\\%x", (LPCWSTR)graphDir, fileId);
		mFiletype = (uint)GetFileType(file);
		mTotalLines = 0;
		InitFiles(m_BaseFile);
	}

	~VAFileRefrences()
	{
		WriteNodes();
		mLinkFile.Close();
		mNodeFile.Close();
	}

	CStringW m_BaseFile;

  private:
	struct NodeInfo
	{
		NodeInfo() : mNLines(0), mType(0), mAttribute(0), mFileId(0), mLine(0)
		{
		}
		UINT mNLines;
		UINT mType;
		UINT mAttribute;
		UINT mFileId;
		UINT mLine;
	};

	// 		struct LinkInfo
	// 		{
	// 			UINT linkType;
	// 			int lineNumber;
	// 			int commonScope;
	// 		};

	typedef std::map<WTString, NodeInfo> NodeMap;

#pragma region IReferenceNodesContainer

	virtual void AddLink(DType* refScope, DType* pointsTo, UINT linkType, int lineNumber, MultiParse* mp)
	{
		static ThreadStatic<int> sAddLinkRecurseCnt;

		if (sAddLinkRecurseCnt() > 10)
			return;

		gTypingDevLang = (int)mFiletype;
		if (IsValidSym(refScope))
		{
			WTString from = refScope->SymScope();
			if (from.GetLength() <= 1)
				return;
			NodeInfo* node = &mNodes[from];
			if (!node->mLine)
				node->mLine = (uint)lineNumber;
			if (!node->mType && refScope->type() == NAMESPACE)
			{
				// Write all namespace string foo.bar.bad....
				for (WTString pscope = StrGetSymScope(from); pscope.GetLength() > 1; pscope = StrGetSymScope(pscope))
					mNodes[pscope].mType = NAMESPACE;
			}
			if (!node->mType || (refScope->MaskedType() >= Control_First && refScope->MaskedType() <= Control_Last))
				node->mType = refScope->type();
			node->mAttribute |= refScope->Attributes();
			if ((linkType & (Override | EventCB)) == 0) // reversed links not defined in this file
				node->mFileId = mFileId;
			if (IsValidSym(pointsTo))
			{
				WTString to = pointsTo->SymScope();
				if (to == from && pointsTo->type() == PROPERTY)
					return; // self links in prpoerty caused by "value", could have fixed in vaparse, but safer to add
					        // this cheap fix here. -Jer
				NodeInfo* tonode = &mNodes[to];
				if (!tonode->mType ||
				    (pointsTo->MaskedType() >= Control_First && pointsTo->MaskedType() <= Control_Last))
					tonode->mType = pointsTo->type();
				if (!tonode->mLine && to.contains(from))
					tonode->mLine = (uint)lineNumber;
				tonode->mAttribute |= tonode->mAttribute;
				tonode->mFileId = pointsTo->FileId();

				{
					// simplified addlink adds all links marking common scope and dir in the link fields
					uint fromScopeId = CGHashKey(StrGetSymScope(from));
					uint toScopeId = CGHashKey(StrGetSymScope(to));

					UINT commonDirId = mDirId;
					if (toScopeId != fromScopeId)
						commonDirId = CommonDir(refScope->FilePath(),
						                        pointsTo->FilePath()); // TODO: this probably should be cached?

					UINT commonScopeId = fromScopeId;
					if (toScopeId != fromScopeId)
						commonScopeId = CommonScope(from, to);

					if (!refScope->IsType()) // [case: 72374]
						WriteLink((int)CGHashKey(StrGetSymScope(from)), (int)commonScopeId, (int)CGHashKey(to),
						          (int)commonDirId, (int)linkType, lineNumber);
					WriteLink((int)CGHashKey(from), (int)commonScopeId, (int)CGHashKey(to), (int)commonDirId,
					          (int)linkType, lineNumber);
				}
			}
		}
	}

	virtual void OnIncludeFile(CStringW file, int line)
	{
		// 			UINT fileId = gFileIdManager->GetFileId(file);
		// 			LinkInfo *filelink = &mFileLinks[fileId];
		// 			filelink->lineNumber = line;
		// 			filelink->linkType = Includes;
	}

	virtual void SetLinesOfCode(DType* refScope, UINT nLines, int startLine)
	{
		if (refScope)
		{
			WTString symScope = refScope->SymScope();
			while (symScope.GetLength())
			{
				NodeInfo* node = &mNodes[symScope];
				node->mNLines += nLines;
				symScope = StrGetSymScope(symScope);
			}
		}
		mTotalLines += nLines;
	}

#pragma endregion IReferenceNodesContainer

	void InitFiles(LPCWSTR baseFile)
	{
		CStringW dbFile = baseFile;
		mLinkFile.Open(dbFile + L".Links", CFile::typeBinary | CFile::modeCreate | CFile::modeWrite);
		mNodeFile.Open(dbFile + L".Nodes", CFile::typeBinary | CFile::modeCreate | CFile::modeWrite);

		UINT fileId = gFileIdManager->GetFileId(mFile);
		(void)fileId;
		mDirId = CGHashKey(WTString(Path(mFile))); // gFileIdManager->GetFileId(Path(m_file));
	}

	uint CommonDir(CStringW from, CStringW to)
	{
		// TODO: this should be cached using a map to the fileid pair
		WTString fStr = from;
		WTString tStr = to;
		int diffPos = 0, endpos = 0;
		for (; diffPos < fStr.length() && fStr[diffPos] == tStr[diffPos]; diffPos++)
			if (tStr[diffPos] == '\\')
				endpos = diffPos;
		WTString parentTo = fStr.Mid(0, endpos);
		return CGHashKey(parentTo);
	}

	uint CommonScope(WTString fStr, WTString tStr)
	{
		int diffPos = 0, endpos = 0;
		for (; diffPos < fStr.length() && fStr[diffPos] == tStr[diffPos]; diffPos++)
			if (tStr[diffPos] == ':')
				endpos = diffPos;
		WTString parentTo = fStr.Mid(0, endpos);
		return CGHashKey(parentTo);
	}

	uint DirId(uint fileid)
	{
		CStringW file = gFileIdManager->GetFileForUser(fileid);
		WTString dir(Path(file)); // TODO: needs to be WSTR?
		UINT dirId = CGHashKey(dir);
		return dirId;
	}

	// 		int GetCommonPathPos(LPCSTR p1, LPCSTR p2)
	// 		{
	// 			int commonDirPos = 0;
	// 			NOT UNICODE SAFE
	// 			for (int i = 0; p1[i] && tolower(p1[i]) == tolower(p2[i]); i++)
	// 				if (mFile[i] == L'\\' || mFile[i] == L'/')
	// 					commonDirPos = i;
	// 			return commonDirPos;
	// 		}

	LPCSTR CGGetSym(LPCSTR symscope)
	{
		LPCSTR sym = symscope;
		for (LPCSTR p = symscope; *p; p++)
			if (strchr(":\\/", *p))
				sym = p + 1;
		return sym;
	}

	LPCSTR CGGetSym(const WTString& symscope)
	{
		return CGGetSym(symscope.c_str());
	}

	WTString FormatNodeLine(const WTString& symscope, int type, int attr, int lines, int line, int dirId)
	{
		// [case: 67546] hack in support for dtor icon
		// use of V_DESTRUCTOR is for spaghetti only.
		if (attr & V_CONSTRUCTOR)
		{
			for (LPCSTR p = symscope.c_str(); *p; p++)
			{
				if (strchr("~!", *p))
				{
					attr &= ~V_CONSTRUCTOR;
					attr |= V_DESTRUCTOR;
					break;
				}
			}
		}

		uint scopeId = CGScopeId(symscope);

		// case=72795 Fix parent scope of these, or will produce bad tree
		static const uint forewardDeclareId = CGHashKey(":ForwardDeclare");
		if (scopeId == forewardDeclareId)
			scopeId = 0;

		static const uint templateParam = CGHashKey(":TemplateParameter");
		if (scopeId == templateParam)
			scopeId = 0;

		WTString cleanSym = CleanScopeForDisplay(CGGetSym(symscope));
		return FormatNodeLine(CGHashKey(symscope), scopeId /*? scopeId: m_dirId*/, cleanSym, type, attr, lines, line,
		                      dirId);
	}

	WTString FormatNodeLine(uint symId, uint ScopeId, const WTString& sym, int type, int attr, int lines, int line,
	                        int dirid)
	{
		WTString ln;
		ln.WTFormat("%x|%x|%s|%x|%x|%x|%x|%x\r\n", // See: [Nodes] @WholeTomato.VAGraphNS.VAReferencesDB.CreateDBs
		            symId, ScopeId /*?ScopeId:m_dirId*/, sym.c_str(), type, attr, lines, line, dirid);
		return ln;
	}

	WTString CGGetScope(LPCSTR symscope)
	{
		int lscopePos = 0;
		for (int i = 0; symscope[i]; i++)
			if (strchr(":\\/", symscope[i]))
				lscopePos = i;
		return WTString(symscope, lscopePos);
	}

	WTString CGGetScope(const WTString& symscope)
	{
		return CGGetScope(symscope.c_str());
	}

	uint CGScopeId(LPCSTR symscope)
	{
		if (symscope[0] == ':')
		{
			uint hv = CGHashKey(StrGetSymScope(symscope));
			return hv;
		}
		return gFileIdManager->GetFileId(Path(CStringW(WTString(symscope).Wide())));
	}

	uint CGScopeId(const WTString& symscope)
	{
		return CGScopeId(symscope.c_str());
	}

	void WriteLink(int fromId, int commonScopeId, int toId, int commonDirId, int type, int line)
	{
		WTString ln;
		ln.WTFormat("%x|%x|%x|%x|%x|%x|%x\r\n", // See: [Links] @ WholeTomato.VAGraphNS.VAReferencesDB.CreateDBs
		            fromId, commonScopeId, toId, commonDirId, type, line, mDirId);
		mLinkFile.Write(ln.c_str(), (uint)ln.GetLength());
	}

	void WriteNodes()
	{
		UINT fileId = gFileIdManager->GetFileId(mFile);
		WTString ln;

		// Add parent directory nodes
		mNodes[WTString(mFile)].mType = FILE_TYPE;

		auto slnFile = GlobalProject->SolutionFile();
		auto slnDir = Path(slnFile);

		// If the file is in the sln directory (or subdir), then
		// add all folders up to the sln dir.
		if (slnDir.GetLength() > 0 && mFile.Find(slnDir) == 0)
		{
			CStringW dir = mFile;
			while (dir != slnDir)
			{
				dir = ::Path(dir);

				// for safety
				if (::PathIsRootW(dir))
					break;

				mNodes[dir].mType = FOLDER_TYPE;
			}
		}
		else
		{
			mNodes[WTString(Path(mFile))].mType = FOLDER_TYPE;
			mNodes[WTString(Path(Path(mFile)))].mType = FOLDER_TYPE;
		}

		for (NodeMap::iterator it = mNodes.begin(); it != mNodes.end(); ++it)
		{
			if (it->second.mFileId == fileId || it->second.mType == NAMESPACE || it->second.mType == FOLDER_TYPE ||
			    it->second.mType == FILE_TYPE)
			{
				WTString sym = it->first;
				if (it->second.mType == FILE_TYPE || it->second.mType == FOLDER_TYPE)
				{
					int dirid = (int)CGHashKey(WTString(Path(sym.Wide())));
					ln = FormatNodeLine(sym, (int)it->second.mType, (int)it->second.mAttribute, (int)it->second.mNLines,
					                    (int)it->second.mLine, (int)dirid);
				}
				else
					ln = FormatNodeLine(sym, (int)it->second.mType, (int)it->second.mAttribute, (int)it->second.mNLines,
					                    (int)it->second.mLine, (int)mDirId);
				mNodeFile.Write(ln.c_str(), (uint)ln.GetLength());
			}
		}
	}

	bool IsSysSym(DType* refScope)
	{
		return refScope->IsSysLib() || refScope->IsSystemSymbol();
	}

	// 		struct LinkInst
	// 		{
	// 			LinkInst(WTString from, WTString to, UINT type, int lineNumber)
	// 			{
	// 				this->from = from;
	// 				this->to = to;
	// 				this->linkType = type;
	// 				this->lineNumber = lineNumber;
	// 			}
	// 			WTString from;
	// 			WTString to;
	// 			UINT linkType;
	// 			int lineNumber;
	// 		};
	// 		typedef std::list< LinkInst> LinksList;
	// 		LinksList mLinksList;
	//
	// 		typedef std::pair<uint, uint> FilePair;

	NodeMap mNodes;
	int mTotalLines;
	CStringW mFile;
	uint mFileId;
	uint mDirId;
	uint mFiletype;
	CFileW mNodeFile;
	CFileW mLinkFile;
};

class MultiParseProvider
{
  public:
	MultiParseProvider() = default;
	~MultiParseProvider() = default;

	MultiParsePtr GetFileMP(CStringW file)
	{
		if (file != m_file)
		{
			ASSERT(g_mainThread != GetCurrentThreadId());
			DatabaseDirectoryLock l2;
			if (file != m_file) // ensure not set while waiting for lock? case=72603
			{
				m_file = file;
				MultiParsePtr tmp = MultiParse::Create(); //   Get a new before the delete so we don't get the same
				                                          //   address confusing the cache logic.
				m_mp = tmp;
				m_mp->FormatFile(file, V_INFILE | V_INPROJECT, ParseType_Locals, false);
			}
		}
		return m_mp;
	}

  private:
	MultiParsePtr m_mp;
	CStringW m_file;
};

static MultiParseProvider s_MultiParseProvider;

CStringW ReferencesInFile(CStringW file)
{
	CStringW baseFile;
	if (!Is_Some_Other_File(GetFileType(file)) && GetFileType(file) != XML)
	{
		if (gTypingDevLang <= 0)
			gTypingDevLang = GetFileType(file);

		CStringW dbDir = GetDbGraphDir();
		CreateDir(dbDir);
		{
			VAFileRefrences g2(file);
			baseFile = g2.m_BaseFile;
			VAParseCodeGraph::StaticGraphCodeFile(file, "", &g2);
		}
	}
	return baseFile;
}

//////////////////////////////////////////////////////////////////////////

BOOL DTE_GotoFileLine(LPCWSTR file, int ln = 0, LPCWSTR symToHilight = NULL)
{
	CComPtr<EnvDTE::ItemOperations> item;
	CComPtr<EnvDTE::Window> pWin;
	CComPtr<EnvDTE::Document> pDoc;
	CComBSTR fileBstr = file;
	gDte->get_ItemOperations(&item);
	if (item)
		item->OpenFile(fileBstr, CComBSTR(EnvDTE::vsViewKindTextView), &pWin);
	if (pWin && ln > 0) // if passed line == -1, just open file and leave caret where it is.
	{
		CComQIPtr<EnvDTE::TextWindow> pTextWin;
		pWin->get_Document(&pDoc);
		if (pDoc)
		{
			CComPtr<EnvDTE::TextSelection> pSelection;
			pDoc->get_Selection((IDispatch**)&pSelection);
			if (pSelection)
			{
				if (symToHilight && symToHilight[0])
				{
					pSelection->GotoLine(ln, VARIANT_TRUE);
					VARIANT_BOOL found = VARIANT_FALSE;
					pSelection->FindText(CComBSTR(symToHilight),
					                     EnvDTE::vsFindOptionsMatchWholeWord | EnvDTE::vsFindOptionsBackwards, &found);
					if (found == VARIANT_FALSE)
						pSelection->GotoLine(ln, VARIANT_TRUE);
				}
				else
					pSelection->GotoLine(ln, VARIANT_TRUE);
			}
		}
	}
	return TRUE;
}

#define ISPRESSED(key) (GetKeyState(key) & 0x1000)

static ThreadStatic<CStringW> sGraphCallbackRstr;

LPCWSTR VAGraphCallback(LPCWSTR idStrW)
{
	WTString idStr = idStrW;
	token2 t = idStr;
	sGraphCallbackRstr().Empty();
	WTString cmd = t.read('|');

	if (cmd == "ComCleanup")
	{
		// See: DoCleanup();
		if (gDte)
			gDte.Release();
		if (gDte2)
			gDte2.Release();
		FreeTheGlobals();

		return sGraphCallbackRstr();
	}

	if (cmd == "GetFilePath")
	{
		WTString hexstr = t.read('|');
		int fileId = 0;
		sscanf(hexstr.c_str(), "%x", &fileId);
		sGraphCallbackRstr() = gFileIdManager->GetFile((uint)fileId);
		return sGraphCallbackRstr();
	}

	if (cmd == "Goto")
	{
		WTString hexstr = t.read('|');
		int scopeId = 0, symId = 0;
		sscanf(hexstr.c_str(), "%x", &scopeId);
		hexstr = t.read('|');
		sscanf(hexstr.c_str(), "%x", &symId);
		WTString sym = t.read('|');

		MultiParsePtr mp = MultiParse::Create();
		DBQuery it(mp);
		CStringW file = gFileIdManager->GetFile((uint)symId);
		if (file.GetLength())
			DTE_GotoFileLine(file);
		else if (it.FindExactList(sym, (uint)scopeId, MultiParse::DB_ALL))
		{
			DType* dt = it.GetFirst();
			DTE_GotoFileLine(gFileIdManager->GetFile(dt->FileId()), dt->Line(), sym.Wide());
		}
		else if (it.FindExactList(sym, 0, MultiParse::DB_ALL)) // Look in global scope, not just directory
		{
			DType* dt = it.GetFirst();
			DTE_GotoFileLine(gFileIdManager->GetFile(dt->FileId()), dt->Line(), sym.Wide());
		}
		return sGraphCallbackRstr();
	}

	if (cmd == "GetSymDef")
	{
		WTString hexstr = t.read('|');
		int scopeId = 0, symId = 0;
		sscanf(hexstr.c_str(), "%x", &scopeId);
		hexstr = t.read('|');
		sscanf(hexstr.c_str(), "%x", &symId);
		WTString sym = t.read('|');
		if (gTypingDevLang == -1)
			gTypingDevLang = Plain;
		MultiParsePtr mp = MultiParse::Create();
		DBQuery it(mp);

		if (it.FindExactList(sym, (uint)scopeId, MultiParse::DB_ALL))
		{
			DType* dt = it.GetFirst();
			if (dt)
			{
				WTString def = dt->Def();
				int fileType = GetFileType(dt->FilePath());

				def = CleanDefForDisplay(def, fileType);
				sGraphCallbackRstr() = def.Wide();
			}
		}
		return sGraphCallbackRstr();
	}

	if (cmd == "GetFileId")
	{
		CStringW file = idStrW + strlen("GetFileId|");
		if (gTypingDevLang == -1)
			gTypingDevLang = GetFileType(file);
		CString__FormatW(sGraphCallbackRstr(), L"%x", gFileIdManager->GetFileId(file));
		return sGraphCallbackRstr();
	}

	if (cmd == "ParseFile")
	{
		CStringW dbDir = GetDbGraphDir();
		CreateDir(dbDir);
		while (GlobalProject->IsBusy()) // case=72580
			Sleep(200);
		CStringW file = idStrW + strlen("ParseFile|");
		if (IsFile(file))
			sGraphCallbackRstr() = ReferencesInFile(file);
		return sGraphCallbackRstr();
	}

	if (cmd == "InvalidateFile")
	{
		CStringW file = idStrW + strlen("InvalidateFile|");
		if (IsFile(file))
			InvalidateFileDateThread(file);
		return sGraphCallbackRstr();
	}

	if (cmd == "GetSlnGraphFileName")
	{
		GlobalProject->LaunchProjectLoaderThread(CStringW());
		CStringW dbDir = GetDbGraphDir();
		CreateDir(dbDir);
		sGraphCallbackRstr() = dbDir + L"\\Spaghetti.db";
		return sGraphCallbackRstr();
	}

	if (cmd == "GetDbDir")
	{
		sGraphCallbackRstr() = VaDirs::GetDbDir();
		return sGraphCallbackRstr();
	}

	if (cmd == "RemoveFileFromVaDB")
	{
		CStringW dbDir = GetDbGraphDir();
		CreateDir(dbDir);
		CStringW file = idStrW + strlen("ParseFile|");

		ASSERT(g_mainThread != GetCurrentThreadId());
		MultiParsePtr mp = MultiParse::Create(gTypingDevLang);
		DatabaseDirectoryLock l2;
		mp->RemoveAllDefs(file, DTypeDbScope::dbSlnAndSys);

		return sGraphCallbackRstr();
	}

	if (cmd == "IncludeSystemSymbols")
	{
		g_includeGlobalRefs = true;
		return sGraphCallbackRstr();
	}

	if (cmd == "GetVAGraphVersion")
	{
		// See: WholeTomato.VAGraphNS.VAInteropBase.LoadVAXDLL
		CString__FormatW(sGraphCallbackRstr(), L"%d|%d", VA_VER_BUILD_NUMBER, 3);
		return sGraphCallbackRstr();
	}

	if (cmd == "GetScopeFromLine")
	{
		int line, col, fileId;
		sscanf(t.c_str().c_str(), "|%x|%x|%x", &fileId, &line, &col);
		CStringW filePath = gFileIdManager->GetFile((uint)fileId);
		WTString buf = GetFileText(filePath);
		if (!buf.GetLength())
			return sGraphCallbackRstr();
		int pos = 0;
		for (int l = line - 1; l && buf[pos]; pos++)
			if (buf[pos] == '\n')
				l--;
		WTString scope = QuickScopeLine(buf, ULONG(line - 1), col, GetFileType(filePath));
		sGraphCallbackRstr() = scope.Wide();
		return sGraphCallbackRstr();
	}

	if (cmd == "GetScopeContextString")
	{
		int line, fileId;
		sscanf(t.c_str().c_str(), "|%x|%x", &fileId, &line);
		CStringW filePath = gFileIdManager->GetFile((uint)fileId);
		WTString buf = GetFileText(filePath);
		if (!buf.GetLength())
			return sGraphCallbackRstr();
		int pos = 0;
		for (int l = line - 1; l && buf[pos]; pos++)
			if (buf[pos] == '\n')
				l--;
		sGraphCallbackRstr() = QuickScopeContext(buf, ULONG(line - 1), 0, GetFileType(filePath)).Wide();
		return sGraphCallbackRstr();
	}

	if (cmd == "GotoLineFile")
	{
		int line, fileId;
		sscanf(t.c_str().c_str(), "|%x|%x", &fileId, &line);
		t.read('|');
		t.read();
		WTString sym = t.read();
		CStringW filePath = gFileIdManager->GetFile((uint)fileId);
		if (filePath.GetLength())
			DTE_GotoFileLine(filePath, line, sym.Wide());
		return sGraphCallbackRstr();
	}

	if (cmd == "StopParse")
	{
		// TODO: gracefully kill "ParseFile"?
		// vainterop "ParseFile" needs to be notified and not load incomplete reference files
		return sGraphCallbackRstr();
	}

	if (cmd == "SetStatus")
	{
		SetStatus(t.read('|'));
		return sGraphCallbackRstr();
	}

	if (cmd == "StartSolutionParse")
	{
		CreateDir(GetDbGraphDir());
		if (g_CodeGraphWithOutVA)
			GlobalProject->LaunchProjectLoaderThread(CStringW());

		return sGraphCallbackRstr();
	}

	if (cmd == "InitDll")
	{
		// Todo: Load project from main thread
		g_CodeGraphWithOutVA = TRUE;
		// GlobalProject->LaunchProjectLoaderThread(CStringW());
		return sGraphCallbackRstr();
	}

	if (cmd == "Shutdown")
	{
		if (g_CodeGraphWithOutVA)
		{
			g_statBar = NULL;
			gVaAddinClient.Shutdown();
		}
		return sGraphCallbackRstr();
	}

	if (cmd == "GetDbDir")
	{
		sGraphCallbackRstr() = GetDbGraphDir();
		CreateDir(sGraphCallbackRstr());
		return sGraphCallbackRstr();
	}

	if (cmd == "GetScopeInfoFromLineCol")
	{
		SetStatus("Ready");

		int line, col, fileId;
		sscanf(t.c_str().c_str(), "|%x|%x|%x", &fileId, &line, &col);
		CStringW filePath = gFileIdManager->GetFile((uint)fileId);
		WTString buf = GetFileText(filePath);
		if (!buf.GetLength())
			return sGraphCallbackRstr();
		int pos = 0;
		for (int l = line - 2; l && buf[pos]; pos++)
			if (buf[pos] == '\n')
				l--;
		MultiParsePtr mp = s_MultiParseProvider.GetFileMP(filePath);
		gTestsActive = true; // Prevent a user event from interrupting
		mp->SetCacheable(TRUE);
		WTString scope = TokenGetField(MPGetScope(buf, mp, pos + col, line - 20), "-");
		DTypePtr dt = mp->GetScopeInfo().GetCwData();

		gTypingDevLang = GetFileType(filePath);

		if (IsValidSym(dt.get()))
			sGraphCallbackRstr() = dt->SymScope().Wide();
		else
		{
			if (dt && dt->HasLocalScope())
			{
				WTString err = "Spaghetti is not available for local variables.";
				SetStatus(err);
				MessageBeep(0xffffffff);
			}
			sGraphCallbackRstr() = scope.Wide();
		}
		return sGraphCallbackRstr();
	}

	if (cmd == "GetSetting")
	{
		WTString name = t.read('|');
		WTString defaultValue = t.read('|');
		sGraphCallbackRstr() = GetRegValue(HKEY_CURRENT_USER, ID_RK_APP, name.c_str(), defaultValue.c_str());
		return sGraphCallbackRstr();
	}

	if (cmd == "SetSetting")
	{
		WTString name = t.read('|');
		WTString value = t.read('|');
		SetRegValue(HKEY_CURRENT_USER, ID_RK_APP, name.c_str(), value.c_str());
		return sGraphCallbackRstr();
	}

	if (cmd == "GetVAScopeFromFilePos")
	{
		CStringW file = t.read().Wide();
		if (!IsFile(file))
			return sGraphCallbackRstr();

		long pos = atoi(t.read().c_str());
		WTString pscope;
		if (!g_currentEdCnt)
			gTypingDevLang = GetFileType(file);
		gTestsActive = true; // Prevent a user event from interrupting
		int edPos = g_currentEdCnt ? g_currentEdCnt->GetBufIndex((long)g_currentEdCnt->CurPos()) : pos;
		MultiParsePtr mp = g_currentEdCnt ? g_currentEdCnt->GetParseDb() : s_MultiParseProvider.GetFileMP(file);
		WTString ftext = g_currentEdCnt ? g_currentEdCnt->GetBuf() : GetFileText(file);

		while (GlobalProject->IsBusy())
			Sleep(200);
		DTypePtr dt;
		{
			ASSERT(g_mainThread != GetCurrentThreadId());
			DatabaseDirectoryLock l2;
			if (1)
			{
				MPGetScope(ftext, mp, edPos);
				dt = mp->GetScopeInfo().GetCwData();
				if (!dt)
				{
					dt = SymFromPos(ftext, mp, edPos, pscope, false);
					ASSERT(!dt);
				}
			}
			else
				dt = SymFromPos(ftext, mp, edPos, pscope);
		}

		gTestsActive = false;

		if (dt && (dt->HasLocalScope() || dt->Def() == " " || dt->IsSystemSymbol()))
		{
			// TODO: need to cancle graph from loading on managed side if scope=0xffffffff
			SetStatus("Symbol not found in project");
			MessageBeep(0xffffffff);
			CStringW scopeInfoStr;
			CString__FormatW(scopeInfoStr, L"%s|%x|%x|%x", (LPCWSTR)GetDbGraphDir(), -1, -1,
			                 gFileIdManager->GetFileId(file));
			sGraphCallbackRstr() = scopeInfoStr;
			return sGraphCallbackRstr();
		}
		if (!dt || dt->HasLocalScope() || dt->Def() == " ")
		{
			DType* tmp = mp->FindExact(TokenGetField(pscope, "-")); // Get global scope
			if (tmp)
				dt = std::make_shared<DType>(tmp);
			else
				dt.reset();
		}
		WTString sFileList;
		if (TRUE)
		{
			if (dt && dt->IsSystemSymbol())
			{
				SetStatus("Symbol not found in project");
				MessageBeep(0xffffffff);
			}

			CStringW scopeInfoStr;

			CPoint pt = g_currentEdCnt ? g_currentEdCnt->vGetCaretPos() : CPoint();
			pscope = TokenGetField(pscope, "-"); // Get global scope
			int pscopeId = (pscope.length() > 1) ? (int)CGHashKey(pscope) : 0;
			// See: WholeTomato.VAGraphNS.VAInterop.GetScopeInfo
			int symId = dt ? (int)CGHashKey(dt->SymScope()) : 0;
			CString__FormatW(scopeInfoStr, L"%s|%x|%x|%x", (LPCWSTR)GetDbGraphDir(), pscopeId, symId,
			                 gFileIdManager->GetFileId(file));

			sGraphCallbackRstr() = scopeInfoStr;
		}
		return sGraphCallbackRstr();
	}
	_ASSERTE(FALSE);
	return sGraphCallbackRstr();
}
} // namespace CodeGraphNS

#if !defined(RAD_STUDIO)
extern "C" __declspec(dllexport) LPCWSTR VAGraphCallback(LPCWSTR idStrW)
{
	return CodeGraphNS::VAGraphCallback(idStrW);
}
#endif
