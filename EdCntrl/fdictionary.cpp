#include "stdafxed.h"
#include <ppl.h>
#include <locale.h>
#include <algorithm>
#include "mparse.h"
#include "QSort.h"
#include "expansion.h"
#include "resource.h"
#include "timer.h"
#include "DBLock.h"
#include "wtcsym.h"
#include "VACompletionBox.h"
#include "VAClassView.h"
#include "project.h"
#include "file.h"
#include "wt_stdlib.h"
#include "assert_once.h"
#include "FileTypes.h"
#include "Settings.h"
#include "DatabaseDirectoryLock.h"
#include "ParseThrd.h"
#include "Lock.h"
#include "FileId.h"
#include "WtException.h"
#include "Directories.h"
#include "VAHashTable.h"
#include "VACompletionSet.h"
#include "mainThread.h"
#include "Guesses.h"
#include "SymbolPositions.h"
#include "TokenW.h"
#include "BaseClassList.h"
#include "VARefactor.h"
#include "../common/ThreadStatic.h"
#include "DBFile/VADBFile.h"
#include "LogElapsedTime.h"
#include "VAAutomation.h"
#include "SpinCriticalSection.h"
#include "serial_for.h"

#if defined(RAD_STUDIO)
#include "RadStudioPlugin.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using OWL::string;

using std::filebuf;
using std::ifstream;
using std::ios;
using std::ofstream;
using std::streampos;

static const int kMaxDefClsLineLength = 128;
static const size_t kMaxTemplateItemsThatCanBeRemoved = 50;
static parallel_flat_hash_set_N<size_t, 4, std::shared_mutex> sPreviousMap;
static parallel_flat_hash_set_N<size_t, 4, std::shared_mutex> sTemplatesAlreadyAdded;
static const WTString kEmptyTemplate("< >");
static const int kMaxDefListFindCnt = 50;
static const int kUnmatchedScopeMaxFindCnt = 10;
extern FileDic* s_pSysDic;

#undef IDS_MAKING_TEMPLATE
#define IDS_MAKING_TEMPLATE "Creating instance of template %s"
#undef IDS_READY
#define IDS_READY "Ready"


void SubstituteFunctionTemplateInstanceDefaults(const WTString& functionTemplateFormalDeclarationArguments,
                                                const WTString& classTemplateFormalDeclarationArguments, token2& def);

#define HASH_NO_CASE(c) (c & 0x00ffffff)

void ClearFdicStatics()
{
	sPreviousMap.clear();
	sTemplatesAlreadyAdded.clear();
}

UINT WTHashKeyW(LPCWSTR key)
{
	UINT nHash = 0;
	while (*key)
		nHashAddW(*key++);
	return nHash;
}

UINT WTHashKeyNoCaseW(LPCWSTR key)
{
	return WTHashKeyW(key) & HASHCASEMASK;
}

// see if list of \f separated items contains item
LPCSTR ContainsField(LPCSTR defs, LPCSTR def)
{
	LPCSTR pos = strstr(defs, def);
	if (pos)
	{
		if (defs != pos && pos[-1] != '\f')
			return strstrWholeWord(&pos[1], def);
		size_t len = strlen(def);
		if (pos[len] && pos[len] != '\f')
			return strstrWholeWord(&pos[1], def);
	}
	return pos;
}

// static bool
// CanOpenUnsortedLetterFiles(const FileList &unsortedLetterFiles)
//{
//	// see if other we can remove tmp files, no other instances running,
//	// last one to exit will sort...
//	// if we can get exclusive read write access, then no other instances running
////	for (const auto & it : unsortedLetterFiles)
//	if(!unsortedLetterFiles.empty())
//	{
//		HANDLE fp = CreateFileW(/*it*/unsortedLetterFiles.cbegin()->mFilename, GENERIC_READ|GENERIC_WRITE, 0, nullptr,
//			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
//		if (INVALID_HANDLE_VALUE == fp)
//			return false;
//		CloseHandle(fp);
//		return true;
//	}
//
//	return false;
//}

void DbMtxSort(const CStringW& dbDir);
void DbMtxPrune(const CStringW& dbDir);

static void SortLetterFiles(const CStringW& theDbDir)
{
	g_SortBins.Close();
	LOG2("SortLetterFiles");
#if 1
	// [case: 118533]
	DbMtxSort(theDbDir);
	// body moved to Server_SortLetterFiles in ../DbMtx/DbMtxImpl.cpp
#else
	FileList unsortedLetterFiles;
	// LETTER_FILE_UNSORTED => L"VA_?.tmp"
	FindFiles(theDbDir, L"VA_?.tmp", unsortedLetterFiles);
	if (!CanOpenUnsortedLetterFiles(unsortedLetterFiles))
		return;

	if (Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, "C"); // use 'C' sort conventions

	// http://msdn.microsoft.com/en-us/library/dd470426.aspx
	auto sorter = [&](FileInfo& curFile) {
		CLEARERRNO;
		if (IsFile(curFile.mFilename) && GetFSize(curFile.mFilename))
		{
			CStringW letterFile(curFile.mFilename);
			letterFile = letterFile.Left(letterFile.GetLength() - 3);
			letterFile += L"idx";
			Append(curFile.mFilename, letterFile);
			QSort(letterFile);
		}
		if (errno || _doserrno)
		{
			Log(ERRORSTRING);
		}
		_wremove(curFile.mFilename);
	};

	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(unsortedLetterFiles.begin(), unsortedLetterFiles.end(), sorter);
	else
		std::for_each(unsortedLetterFiles.begin(), unsortedLetterFiles.end(), sorter);

	if (Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, ""); // restore
#endif
}

static void PruneCacheFiles()
{
	const CStringW theDbDir(VaDirs::GetDbDir() + kDbIdDir[DbTypeId_ExternalOther] + L"\\");

	g_SortBins.Close();
	LOG2("PruneCacheFiles");
#if 1
	// [case: 118533]
	DbMtxPrune(theDbDir);
	// body moved to Server_PruneCacheFiles in ../DbMtx/DbMtxImpl.cpp
#else
	FileList unsortedLetterFiles;
	// LETTER_FILE_UNSORTED => L"VA_?.tmp"
	FindFiles(theDbDir, L"VA_?.tmp", unsortedLetterFiles);
	if (!CanOpenUnsortedLetterFiles(unsortedLetterFiles))
		return;

	if (Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, "C"); // use 'C' sort conventions

	// http://msdn.microsoft.com/en-us/library/dd470426.aspx
	auto sorter = [&](FileInfo& curFile) {
		try
		{
			CLEARERRNO;
			if (IsFile(curFile.mFilename) && GetFSize(curFile.mFilename))
				QSort(curFile.mFilename);
			if (errno || _doserrno)
				Log(ERRORSTRING);
		}
		catch (...)
		{
			Log("ERROR: exception caught in PruneCacheFiles");
		}
	};

	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(unsortedLetterFiles.begin(), unsortedLetterFiles.end(), sorter);
	else
		std::for_each(unsortedLetterFiles.begin(), unsortedLetterFiles.end(), sorter);

	if (Psettings->m_doLocaleChange)
		setlocale(LC_CTYPE, ""); // restore
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dic
/////////////////////////////////////////////////////////////////////////////////////////////////////////

void Dic::GetMembersList(const WTString& sym, const WTString& scope, WTString& baseclasslist, ExpansionData* lb,
                         bool exact)
{
	LOG(("Dic::ScanLst " + baseclasslist).c_str());
	LogElapsedTime let("D::GML", 100);
	LOCKBLOCK(m_lock);
	unsigned sz = mHashTable->TableSize();

	ScopeHashAry curscope(scope.length() ? scope.c_str() : NULL, baseclasslist, NULLSTR);
	for (unsigned n = 0; n < sz; n++)
	{
		//		int symlength = sym.length();

		for (DefObj* cur = mHashTable->GetRowHead(n); NULL != cur; cur = cur->next)
		{
			const int rank = curscope.Contains(cur->ScopeHash());
			if (!rank)
				continue;

			WTString curSymScope(cur->SymScope());
			LPCTSTR s2 = curSymScope.c_str();
			// Filter out all BCL's because they get should not be added to the list, they break case correct in some
			// instances.
			if (s2[0] != '*')
			{
				if (cur->MaskedType() == RESWORD)
					continue;

				if (cur->IsHideFromUser())
					continue;

				if (exact)
				{
					if (_tcsicmp(sym.c_str(), StrGetSym(s2)) == 0)
						lb->AddStringAndSort(StrGetSym(s2), cur->MaskedType(), cur->Attributes(), cur->SymHash(),
						                     cur->ScopeHash());
				}
				else if (s2[0] == DB_SEP_CHR && rank > -1000)
					lb->AddStringAndSort(StrGetSym(s2), cur->MaskedType(), cur->Attributes(), cur->SymHash(),
					                     cur->ScopeHash());
			}
		}
	}
}

void Dic::add(const WTString& s, const WTString& d, uint type /*= 0*/, uint symAttrs /*= 0*/, uint symDbFlags /*= 0*/)
{
	_ASSERTE((type & TYPEMASK) == type);
	_ASSERTE((symDbFlags & VA_DB_FLAGS) == symDbFlags);

	LOCKBLOCK(m_lock);
	if (s.c_str()[0] != DB_SEP_CHR)
	{
		// always update
		DType* found = mHashTable->Find(DType::GetSymHash(s));
		if (found && found->SymScope() == s)
		{
			// don't append filetimes or BaseClassList
			if (s[0] != DB_SEP_CHR)
			{
				found->setType(type, symAttrs, symDbFlags);
				found->SetDef(d);
			}
			else if (!found->Def().contains(d.c_str()))
			{
				uint type2 = found->MaskedType();
				found->SetDef(d);
				found->setType(type2, 0, 0);
			}
			return;
		}
	}

	mHashTable->CreateEntry(DType(s, d, type, symAttrs, symDbFlags, 0, 0));
}

void Dic::RemoveVaMacros()
{
	if (!mHashTable->GetItemsInContainer())
		return;

	VAHashTable::do_detach_locks_t do_detach_locks;
	auto remover = [&](uint curRowIdx) {
		VAHashTable::Row* row = mHashTable->GetRow(curRowIdx);
		DefObj* prev = nullptr;
		DefObj* cursor = row ? row->Head() : nullptr;
		while (cursor)
		{
			DefObj* next = cursor->next;
			if ((VaMacroDefArg == cursor->type() || VaMacroDefNoArg == cursor->type()) &&
			    !cursor->IsDbBackedByDataFile() && !(cursor->Attributes() & V_VA_STDAFX))
			{
				mHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
				_ASSERTE(!prev || prev->next == next);
			}
			else
				prev = cursor;

			if (cursor == next)
				break;

			cursor = next;
		}
	};

#pragma warning(push)
#pragma warning(disable : 4127)
	LOCKBLOCK(m_lock);
	const uint sz = mHashTable->TableSize();
	if (true || Psettings->mUsePpl)
		Concurrency::parallel_for((uint)0, sz, remover);
	else
		::serial_for<uint>((uint)0, sz, remover);
#pragma warning(pop)
}

// WTString Dic::find(WTString &s, int &type, int flag){
// 	LOCKBLOCK(m_lock);
// 	DType *found = CD->Find(DType(s, NULLSTR));
// 	WTString str;
// 	if(found){
// 		type = found->Type;
// 		str = found->Value();
// 	} else
// 		type = 0;
// 	return(str);
// }

void Dic::MakeTemplate(const WTString& templateName, const WTString& instanceDeclaration,
                       const WTString& constTemplateFormalDeclarationArguments, const WTString& constInstanceArguments)
{
	LOCKBLOCK(m_lock);
	///////////////////////////
	// pass in ":foo" and ":foo<classes>
	// copy all ":foo:*" to ":foo<classes>*"
	LOG("MakeTemplate");
	SetStatusQueued(IDS_MAKING_TEMPLATE, DecodeScope(instanceDeclaration.c_str()).c_str());
	int skey = (int)WTHashKey(&templateName.c_str()[1]);
	token2 templateFormalDeclarationArguments = constTemplateFormalDeclarationArguments;
	templateFormalDeclarationArguments.ReplaceAll("class", "", true);
	int classscope = 0;
	const int scopePos = templateName.ReverseFind(DB_SEP_CHR);
	if (scopePos > 0)
	{
		classscope = (int)WTHashKey(templateName.Mid(0, scopePos).c_str());
		skey = (int)WTHashKey(templateName.Mid(scopePos + 1).c_str());
	}

	templateFormalDeclarationArguments.ReplaceAll("typename", "", true);
	WTString classScope = templateName + DB_SEP_STR;
	uint classScopeLen = (uint)classScope.length();
	try
	{
		uint sz = mHashTable->TableSize();

		for (uint r = 0; !StopIt && r < sz; r++)
		{
			for (DefObj* cursor = mHashTable->GetRowHead(r); cursor && !StopIt; cursor = cursor->next)
			{
				WTString cursorSymScope(cursor->SymScope());
				if (cursorSymScope == templateName ||
				    strncmp(cursorSymScope.c_str(), classScope.c_str(), classScopeLen) == 0)
				{
					if (cursorSymScope.Find(EncodeChar('<')) != -1)
						continue; // Prevent recursion
					if (cursorSymScope.Find('-') != -1 && cursorSymScope.Find("->") == -1)
						continue; // No need to do local variables

					// copy and replace def
					// replace template placeholder T's with instance args in def
					WTString sym = instanceDeclaration + cursorSymScope.Mid(templateName.GetLength());
					BOOL didReplace = FALSE;
					bool toTypeIsPtr = false;
					token2 def = cursor->Def();
					SubstituteTemplateInstanceDefText(templateFormalDeclarationArguments.Str(), constInstanceArguments,
					                                  def, toTypeIsPtr, didReplace);

					if (cursorSymScope == templateName)
					{
						WTString defStr(def.Str());
						if (!defStr.IsEmpty())
						{
							// http://msdn.microsoft.com/en-us/library/6w96b5h7.aspx
							if (defStr[0] == 'p')
							{
								// remove class access
								if (0 == defStr.Find("private "))
									defStr = defStr.Mid(8);
								else if (0 == defStr.Find("public "))
									defStr = defStr.Mid(7);
							}

							// remove ref or value keyword
							if (defStr[0] == 'r' && 0 == defStr.Find("ref "))
								defStr = defStr.Mid(4);
							else if (defStr[0] == 'v' && 0 == defStr.Find("value "))
								defStr = defStr.Mid(6);
						}

						if (defStr.Find("class ") == 0 || defStr.Find("struct ") == 0)
						{
							// class definition
							// remove original template name from this instance
							def.read(':'); // strip "class name : "
							// adding class here does funny things to baseclass searches
							// for example, if def is "class foo{...}", this changes def to "class "
							// need to look into this in further detail to see if really necessary
							def = (WTString("class ") + def.Str()).c_str();
							if (def.Str() == "class ")
							{
								if (defStr.Find("{...}") != -1)
									def = "{...}";
								else
									def = "";
							}
						}
					}

					uint dbFlags = cursor->DbFlags();
					if (dbFlags & VA_DB_BackedByDatafile)
					{
						// instantiated template items are not from db files
						dbFlags ^= VA_DB_BackedByDatafile;
					}

					uint newAttrs = cursor->Attributes() | V_TEMPLATE_ITEM;
					// [case: 23934] update type if template param was a pointer
					if (toTypeIsPtr && !cursor->IsPointer())
						newAttrs |= V_POINTER;
					// add original def as fileID so we can goto its def instead...
					// also add to global dictionary since ldic is temporary...
					add(sym, def.Str(), cursor->MaskedType(), newAttrs, dbFlags);
					g_pGlobDic->add(sym, def.Str(), cursor->MaskedType(), newAttrs, dbFlags, cursor->FileId(),
					                cursor->Line());
				}
			}
		}
		SetStatusQueued(IDS_READY);
	}
	catch (const UnloadingException&)
	{
	}
#if !defined(SEAN)
	catch (...)
	{
		// shutting down?
		VALOGEXCEPTION("FOO:");
		Log("ERROR: exception caught in FD::MakeTemplate");
	}
#endif // !SEAN
}

// find def add to list

extern int ContainsSubset(LPCSTR text, LPCSTR subText, UINT flag);

bool Dic::GetHint(const WTString& sym, const WTString& scope, WTString& baselist, WTString& hintstr)
{
	LOG(("Dic::ScanLst " + baselist).c_str());

	CSpinCriticalSection localGuessesLock;
	std::vector<WTString> localGuesses;

	{
		ScopeHashAry curscope(scope.length() ? scope.c_str() : NULL, baselist, NULLSTR);

		// format scope from foo.bar to foo:bar
		BOOL matchCase = FALSE;
		for (int i = 0; !matchCase && i < sym.GetLength(); i++)
			if (wt_isupper(sym[i]))
				matchCase = TRUE;

		LOCKBLOCK(m_lock);
		uint numRows = mHashTable->TableSize();
		auto getter = [&](uint r) {
			for (DefObj* cur = mHashTable->GetRowHead(r); nullptr != cur; cur = cur->next)
			{
				if (cur->Attributes() & V_HIDEFROMUSER)
					continue;

				const int rank = curscope.Contains(cur->ScopeHash());
				if (rank && !cur->HasFileFlag())
				{
					const WTString csym(cur->Sym());
					int match = ContainsSubset(csym.c_str(), sym.c_str(), 0x2u | (matchCase ? 1u : 0u));
					if (match && match <= 8)
					{
						AutoLockCs l(localGuessesLock);
						localGuesses.push_back(csym);
						// per comment in HintThread::Run, get more guesses, then
						// limit in completion set
						if (localGuesses.size() >= size_t(Psettings->m_nDisplayXSuggestions * 2))
							return;
					}
				}
			}
		};

#pragma warning(push)
#pragma warning(disable : 4127)
		if (true || Psettings->mUsePpl)
			Concurrency::parallel_for((uint)0, numRows, getter);
		else
			::serial_for<uint>((uint)0, numRows, getter);
#pragma warning(pop)
	}

	if (localGuesses.size())
	{
		AutoLockCs l(g_Guesses.GetLock());
		for (auto& csym : localGuesses)
		{
			g_Guesses.AddGuess(CompletionSetEntry(csym.c_str()));
			if (g_Guesses.GetMostLikelyGuess().IsEmpty())
				g_Guesses.SetMostLikely(csym);
		}
	}

	return g_Guesses.GetCount() > 0;
}

// See if sym is defined anywhere, for ASC
uint Dic::FindAny(const WTString& sym, BOOL allowGoToMethod /* = FALSE */)
{
	LOCKBLOCK(m_lock);
	LPCTSTR s1 = sym.c_str();
	uint rval = 0;
	const uint symdefSymHash = DType::GetSymHash(sym);
	for (DefObj* cur = mHashTable->FindRowHead(symdefSymHash); NULL != cur; cur = cur->next)
	{
		// changed to case sensitive hash compare
		if (cur->SymHash() != symdefSymHash)
		{
			// no match
			continue;
		}

		const uint type = cur->MaskedType();
		switch (type)
		{
		case VaMacroDefArg:
		case VaMacroDefNoArg:
		case CachedBaseclassList:
		case TEMPLATE_DEDUCTION_GUIDE:
			continue;
		}
		// allow V_HIDEFROMUSER for forward declarations

		// do we need this???  is it for hash collisions???
		int p1 = sym.length();
		WTString curSymScope(cur->SymScope());
		int p2 = curSymScope.length();
		LPCTSTR s2 = curSymScope.c_str();
		// reverse cmp
		while (p1 && p2 && s1[p1 - 1] == s2[p2 - 1])
		{
			p1--;
			p2--;
		}

		if (p1 < 1)
		{
			// sym matches
			if (type == CLASS || type == STRUCT)
				return CLASS;
			if (!rval && (allowGoToMethod || !cur->Def().contains("{...}"))) // don't use goto {...} info for coloring
				rval = type;
		}
		else
		{
			// hash collision
		}
	}
	return rval;
}

DType* Dic::FindAnySym(const WTString& sym)
{
	LOCKBLOCK(m_lock);
	LPCTSTR s1 = sym.c_str();
	DType* guess = NULL;
	const uint symdefSymHash = DType::GetSymHash(sym);

	for (DefObj* cur = mHashTable->FindRowHead(symdefSymHash); NULL != cur; cur = cur->next)
	{
		if (cur->SymHash() != symdefSymHash)
			continue;

		const uint type = cur->MaskedType();
		switch (type)
		{
		case VaMacroDefArg:
		case VaMacroDefNoArg:
		case CachedBaseclassList:
		case TEMPLATE_DEDUCTION_GUIDE:
			continue;
		}
		// allow V_HIDEFROMUSER for forward declarations

		int p1 = sym.length();
		WTString symScope(cur->SymScope());

#if defined(RAD_STUDIO)
		// [case: 150126] prevent from mixing VCL and FMX
		if (gVaRadStudioPlugin && gVaRadStudioPlugin->IsUsingFramework())
		{
			auto scope = cur->Scope();
			if (!scope.IsEmpty())
			{
				if (gVaRadStudioPlugin->IsFmxFramework())
				{
					if (scope.FindNoCase(":Vcl:") >= 0)
						continue;
				}
				else if (gVaRadStudioPlugin->IsVclFramework())
				{
					if (scope.FindNoCase(":Fmx:") >= 0)
						continue;
				}
			}
		}
#endif

		int p2 = symScope.length();
		LPCTSTR s2 = symScope.c_str();
		// reverse cmp
		if (s2[0] == DB_SEP_CHR)
		{
			// make sure not *:bcl, or spelling
			while (p1 && p2 && s1[p1 - 1] == s2[p2 - 1])
			{
				p1--;
				p2--;
			}

			if (p1 < 1)
			{
				// sym matches
				if (type == CLASS || type == STRUCT)
					return cur;
				if (!guess)
					guess = cur;
			}
			else
			{
				// hash collision
			}
		}
	}
	return guess;
}

Dic::~Dic()
{
	LOCKBLOCK(m_lock);
	delete mHashTable;
}

Dic::Dic(uint rows) : mHashTable(new VAHashTable(rows))
{
	LOG((WTString("Dic rows") + itos((int)rows)).c_str());
}

void Dic::add(const DType* dtype)
{
	return add(DType(dtype));
}
void Dic::add(DType&& dtype)
{
	_ASSERTE(dtype.MaskedType() || dtype.Attributes());
	LOCKBLOCK(m_lock);
	mHashTable->CreateEntry(std::move(dtype));
}

DType* Dic::Find(FindData* fds)
{
	uint hv = DType::HashSym((*fds->sym).c_str());

	LOCKBLOCK(m_lock);

	LPCSTR sym = (*fds->sym).c_str();
	const int doDBCase = g_doDBCase;

	for (DefObj* cur = mHashTable->FindRowHead(hv); NULL != cur; cur = cur->next)
	{
		DEFTIMER(CD_Find1);
		if ((fds->findFlag & FDF_TYPE) && !cur->IsType())
			continue;
		if (cur->IsConstructor() && !(fds->findFlag & FDF_CONSTRUCTOR))
			continue;

		switch (cur->MaskedType())
		{
		case GOTODEF:
			if (!(fds->findFlag & FDF_GotoDefIsOk))
				continue;
			break;
		case COMMENT:
		case CachedBaseclassList:
		case TEMPLATE_DEDUCTION_GUIDE:
			continue;
		}

		const int r = fds->scopeArray.Contains(cur->ScopeHash());
		if (!r)
			continue;

		const WTString curSym(cur->Sym());
		if (StrCmpAC_local(curSym.c_str(), sym) != 0)
			continue;

		DEFTIMER(CD_Find2);
		if (((r > fds->scoperank) || (r == fds->scoperank && cur->IsPreferredDef())) /* && r != -2000*/)
		{
			if (fds->findFlag & FDF_SkipIgnoredFiles)
			{
				if (ShouldIgnoreFileCached(*cur, true))
					continue;
			}

			fds->scoperank = r;
			fds->record = cur;
		}
	}
	return fds->record;
}

DType* Dic::FindDeclaration(DType& data)
{
	LOCKBLOCK(m_lock);
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		WTString dataSymScope(data.SymScope());
		for (DefObj* cursor = mHashTable->FindRowHead(data.SymHash()); cursor; cursor = cursor->next)
		{
			if (data.ScopeHash() == cursor->ScopeHash())
			{
				DType* curDat = cursor;
				if (curDat && curDat != &data)
				{
					if (curDat->SymHash() == data.SymHash() &&
					    (curDat->FileId() != data.FileId() ||
					     (curDat->FileId() == data.FileId() && curDat->Line() < data.Line())))
					{
						if (curDat->SymScope() == dataSymScope && AreSymbolDefsEquivalent(*curDat, data))
						{
							return curDat;
						}
					}
				}
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("D::FD:");
		Log("D::FD:");
	}
#endif // !SEAN
	return NULL;
}

DType* Dic::FindExact(std::string_view sym, uint scopeID, uint searchForType /*= 0*/, bool honorHideAttr /*= true*/)
{
	DType* res = NULL;

	LOCKBLOCK(m_lock);
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		UINT v = 0;
		if (sym.starts_with(DB_SEP_CHR))
			v = WTHashKey(StrGetSym_sv(sym));
		else
			v = WTHashKey(sym);
		if (!v)
			return NULL;

		const uint symHash2 = DType::GetSymHash2_sv(sym);
		const int doDBCase = g_doDBCase;
		int symMatchSkippedByType = 0;
		for (DefObj* cur = mHashTable->FindRowHead(v); NULL != cur; cur = cur->next)
		{
			const uint curType = cur->MaskedType();
			if (searchForType && searchForType != curType)
			{
				// [case: 71747] scope has to check for macros very often
				// this is an optimization specifically for VaMacroDefArg/VaMacroDefNoArg.
				if (VaMacroDefArg == searchForType || VaMacroDefNoArg == searchForType)
				{
					// VaMacroDef*Arg should always be passed in with a scopeID.
					// otherwise some assumptions may have to be revisited.
					_ASSERTE(scopeID);

					if (cur->SymMatch(symHash2))
					{
						// symMatchSkippedByType is count of hits of type
						// !VaMacroDef*Arg (when looking for VaMacroDef*Arg) with same
						// name as sym we are looking for.
						if (++symMatchSkippedByType > 25)
						{
							// more than 25 hits with same name that aren't
							// VaMacroDef*Arg.  Assume no remaining ones are either.
							return res;
						}
					}
				}
				continue;
			}

			if (honorHideAttr && cur->IsHideFromUser())
			{
				switch (searchForType)
				{
				case CachedBaseclassList:
				case VaMacroDefArg:
				case VaMacroDefNoArg:
				case TEMPLATE_DEDUCTION_GUIDE:
				case TEMPLATETYPE:
					// override honorHideAttr if explicitly looking for a hidden type
					break;
				default:
					continue;
				}
			}

			if (GOTODEF == curType && GOTODEF != searchForType)
			{
				// Ignore GotoDef entries
				continue;
			}

			if (!scopeID)
			{
				if (StrEqualAC_local_sv(cur->SymScope_sv().first, sym))
					return cur;
			}

			if (cur->ScopeHash() == scopeID)
			{
				if (StrEqualAC_local_sv(cur->Sym_sv().first, sym))
				{
					if (gTypingDevLang == VBS || gTypingDevLang == JS)
						if (cur->Def_sv().first.contains('='))
							return cur; // return assignment "foo = type" before decl "var foo"
					res = cur;

					_ASSERTE((VaMacroDefArg != curType && VaMacroDefNoArg != curType) || cur == res);
				}
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FD:D:FE:");
		Log("FindExact Exception");
	}
#endif // !SEAN

	return res;
}

BOOL Dic::HasMultipleDefines(LPCSTR sym)
{
	UINT symID = WTHashKey(StrGetSym(sym));
	UINT scopeID = WTHashKey(StrGetSymScope(sym));
	BOOL hasMatch = FALSE;
	LOCKBLOCK(m_lock);
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		for (DefObj* cur = mHashTable->FindRowHead(symID); NULL != cur; cur = cur->next)
		{
			if (cur->ScopeHash() == scopeID)
			{
				if (hasMatch)
					return TRUE;
				hasMatch = TRUE;
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FD:D:HMD:");
		Log("HasMultipleDefines Exception");
	}
#endif // !SEAN

	return FALSE;
}

int Dic::FindExactList(LPCSTR sym, uint scopeID, DTypeList& cdlist)
{
	UINT hv;
	if (sym[0] == DB_SEP_CHR)
		hv = ::WTHashKey(::StrGetSym(sym));
	else
		hv = ::WTHashKey(sym);
	return FindExactList(hv, scopeID, cdlist);
}

int Dic::FindExactList(uint symHash, uint scopeID, DTypeList& cdlist)
{
	if (!symHash)
		return 0;

	LOCKBLOCK(m_lock);
	const int doDBCase = g_doDBCase;
	UseHashEqualAC_local_fast;
	int cnt = 0;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		for (DefObj* cur = mHashTable->FindRowHead(symHash); NULL != cur; cur = cur->next)
		{
			if (HashEqualAC_local_fast(cur->SymHash(), symHash) && HashEqualAC_local_fast(cur->ScopeHash(), scopeID))
			{
				cdlist.emplace_back(cur);
				++cnt;
			}
			if (cur->next == cur)
				break;
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FD:D:FEL:");
	}
#endif // !SEAN

	return cnt;
}

void Dic::ForEach(DTypeForEachCallback fn, bool& stop)
{
	LOCKBLOCK(m_lock);
	uint numRows = mHashTable->TableSize();
	auto rowFn = [&](uint r) {
		for (DefObj* cur = mHashTable->GetRowHead(r); nullptr != cur && !stop; cur = cur->next)
		{
			fn(cur, false);
		}
	};

#pragma warning(push)
#pragma warning(disable : 4127)
	if (true || Psettings->mUsePpl)
		Concurrency::parallel_for((uint)0, numRows, rowFn);
	else
		::serial_for<uint>((uint)0, numRows, rowFn);
#pragma warning(pop)
}

bool IsFromInvalidSystemFile(const DType* cursor)
{
	if (cursor->IsDbCpp() &&                                       // mfc/cpp sysdic
	    !cursor->IsDbSolutionPrivateSystem() &&                    // by definition, DbSolutionPrivateSystem is valid
	    (RESWORD != cursor->type() || cursor->IsHideFromUser()) && // using directives are RESWORD and hidden -- do not
	                                                               // allow to pass without checking
	    !cursor->IsVaStdAfx()) // vastdafx is always applicable, regardless of include dirs
	{
		// [case: 65910]
		UINT fid = cursor->FileId(); // [case: 140857]
		if (fid && !IncludeDirs::IsSystemFile(fid))
		{
			// ignore mfc/cpp sysdic items from not currently applicable include dirs
			return true;
		}
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// FileDic
/////////////////////////////////////////////////////////////////////////////////////////////////////////

FileDic::FileDic(const CStringW& fname, BOOL isSysDic, uint rows)
    : m_loaded(FALSE), m_modified(0), m_pHashTable(new VAHashTable(rows)), m_isSysDic(isSysDic), m_dbDir(fname),
      m_rows(rows), mHashTableOwner(true)
{
	LOG2("FileDic ctor");
}

FileDic::FileDic(const CStringW& fname, VAHashTable* pSharedHashTable)
    : m_loaded(FALSE), m_modified(0), m_pHashTable(pSharedHashTable), m_isSysDic(true), m_dbDir(fname), m_rows(0),
      mHashTableOwner(false)
{
	LOG2("FileDic ctor (shared)");
}

FileDic::~FileDic()
{
#if defined(VA_CPPUNIT) || defined(_DEBUGmem)
	_ASSERTE(_CrtCheckMemory());
#endif // VA_CPPUNIT || _DEBUGmem
	LOG2("FileDic dtor");
	if (m_isSysDic && mHashTableOwner)
	{
		_ASSERTE(!GetDBDir().IsEmpty());
		DatabaseDirectoryLock l2;
		::SetStatus(IDS_SORTING);
		::PruneCacheFiles();
	}
	if (mHashTableOwner)
		delete m_pHashTable;
	m_pHashTable = NULL;
}

DType* FileDic::Find(const WTString& str, int fdFlags /*= FDF_NONE*/)
{
	if (str.length() && str[0] == DB_SEP_CHR)
	{
		WTString sym = StrGetSym(str);
		WTString scope = StrGetSymScope(str);
		FindData fds(&str, nullptr, &scope, nullptr, fdFlags);
		return Find(&fds);
	}
	else
	{
		// looking up path "c:..."
		FindData fds(&str, nullptr, nullptr, nullptr, fdFlags);
		return Find(&fds);
	}
}

// used to idle cycledb
static int g_findCount = 0;
static const WTString kFormfeed("\f");
static const char kFormfeed_ch('\f');
static const uint32_t kMaxDefConcatLen = 2250;
static CSpinCriticalSection sConcatLock;

///////////////////////////////////////
// the real find

template<bool fast>
DType* FileDic::FindImpl(FindData* fds)
{
	DEFTIMER(FindTimer);
	DB_READ_LOCK;
	g_findCount++;
	const bool kIsCfile = IsCFile(gTypingDevLang);
	const bool kUseNewNamespaceLogic = fast ? false : fds->IsUsingNewNamespaceLogic();
	const bool checkInvalidSys =
	    s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable() && !(fds->findFlag & FDF_NoInvalidSysCheck);
	DType* guess = NULL;
	DType* top = NULL;
	// find match in list
	const int doDBCase = fast ? true : g_doDBCase;
	UseHashEqualAC_local_fast;
	int rnk = fds->scoperank;
	int concatCount = 0;
	const uint tSymHash = DType::GetSymHash(*fds->sym);
	WTString newDef;

	DefObj* cursor;
	int r_i = -1; // will contain current scopeArray index
	std::function<DefObj*()> get_next_scope;
	std::function<DefObj*(DefObj*)> get_next_cursor;
	if (fast)
	{
		get_next_scope = [&r_i, &scopeArray = fds->scopeArray, tSymHash, m_pHashTable = this->m_pHashTable] {
			assert(fast);
			DefObj* ret = nullptr;
			while (!ret && (++r_i < scopeArray.GetCount()))
				ret = m_pHashTable->FindScopeSymbolHead(scopeArray.GetHash(r_i), tSymHash);
			return ret;
		};
		get_next_cursor = [&get_next_scope](DefObj* cursor) {
			DefObj* next = VAHashTable::ScopeSymbolNodeToDefObj(cursor->node_scope_sym_index.next);
			if (!next)
				next = get_next_scope();
			return next;
		};
		cursor = get_next_scope();
	}
	else
		cursor = m_pHashTable->FindRowHead(tSymHash);
	for (; cursor; cursor = fast ? get_next_cursor(cursor) : cursor->next)
	{
		if constexpr (!fast)
		{
			if (!(HashEqualAC_local_fast(tSymHash, cursor->SymHash())))
				continue;
		}

		const uint curType = cursor->MaskedType();
		if (COMMENT == curType)
			continue;
		if (TEMPLATE_DEDUCTION_GUIDE == curType)
			continue;

		if (GOTODEF == curType)
		{
			if (!(fds->findFlag & FDF_GotoDefIsOk))
				continue;
		}
		else
		{
			if ((fds->findFlag & FDF_TYPE) && !IS_OBJECT_TYPE(curType))
				continue;
		}

		if (cursor->IsDbCpp() && !kIsCfile)
			continue; // Mixed C++/C#/VB sln share same SYSDB, this filters c++ code in C#/VB
		if (cursor->IsDbNet() && kIsCfile && GlobalProject && !GlobalProject->CppUsesClr() &&
		    !GlobalProject->CppUsesWinRT())
			continue; // Mixed C++/C#/VB sln share same SYSDB, this filters .NET symbols in pure c/c++ code

		if (fds->findFlag & FDF_SkipIgnoredFiles)
		{
			if (ShouldIgnoreFileCached(*cursor, true))
				continue;
		}

		// ScopeHashAry::Contains is a perf hot spot.  Filter on type as
		// much as possible before calling Contains. #seanPerformanceHotSpotFileDicFind
		int r;
		if constexpr(fast)
			r = fds->scopeArray.RfromI(r_i);
		else
			r = fds->scopeArray.Contains(cursor->ScopeHash());
		if (!r && kUseNewNamespaceLogic)
		{
			// [case: 84744]
			// c# relies heavily on using statements. Given:
			//		using System.Collections;
			//		...
			//		ArrayList foo;
			//		foo.Add();
			// Doing a FindSym on Add without a bcl and an incomplete scope (:ArrayList),
			// System.Collections.ArrayList will not be in fds->scopeArray.  But System.Collections
			// is in fds->scopeArray.  Compensate by stripping the sym and outermost
			// scope from cursor if outermost scope is identical to the fds->scope.
			// In this example, System.Collections is in scopeArray, so cursor of
			// System.Collections.ArrayList will be ranked < -1000 (using namespace).
			auto [cscp_sv, cscp_lock] = cursor->Scope_sv();
			auto cscp_sym_sv = ::StrGetSym_sv(cscp_sv);
			int csym_len = int(1 + cscp_sym_sv.length());
			BOOL tryScopeArrayForNamespace = FALSE;
			if (fds->scope->GetLength() == csym_len)
			{
				//WTString csym(DB_SEP_STR + ::StrGetSym_sv(cscp_sv));
				// cursor->Scope (cscp) is ":System:Collections"
				// csym is ":ArrayList"
				// fds->Scope is ":ArrayList" (not ":System:Collections")
				tryScopeArrayForNamespace = fds->scope->begins_with2(DB_SEP_CHR, cscp_sym_sv);
				// try again and see if ":System:Collections" is in scope array as a namespace
				assert(tryScopeArrayForNamespace == ::StartsWith(*fds->scope, DB_SEP_STR + ::StrGetSym_sv(cscp_sv), FALSE));
			}
			else if (csym_len < fds->scope->GetLength())
			{
				//WTString csym(DB_SEP_STR + ::StrGetSym_sv(cscp_sv));
				// [case: 85849]
				// cpp file (namespace Foo { class Bar } in header):
				// using namespace Foo;
				// Bar::Baz() { auto x = mem; } // where mem is a member of Foo::Bar
				//csym += DB_SEP_STR;
				// cursor->Scope (cscp) is ":Foo:Bar"
				// csym is ":Bar:"
				// fds->Scope is ":Bar:Baz-2" (not ":Foo:Bar:Baz-2")
				//tryScopeArrayForNamespace = ::StartsWith(*fds->scope, csym, FALSE);
				tryScopeArrayForNamespace = ((*fds->scope)[1 + cscp_sym_sv.length()] == DB_SEP_CHR) && fds->scope->begins_with2(DB_SEP_CHR, cscp_sym_sv);
				assert(tryScopeArrayForNamespace == ::StartsWith(*fds->scope, DB_SEP_STR + ::StrGetSym_sv(cscp_sv) + DB_SEP_STR, FALSE));
				// try again and see if ":Foo" is in scope array as a namespace
			}

			if (tryScopeArrayForNamespace)
			{
				r = fds->scopeArray.Contains(::WTHashKey(::StrGetSymScope_sv(cscp_sv)));
				if (r >= -1000 || r == -2000)
					r = 0; // this fix should result in r < -1000 and r > -2000, else found something else
			}
		}

		if (RESWORD == curType)
		{
			// TYPE == curType		// type is used in C#, changed all reserved words like char to use RESWORD instead
			// -Jer

			// don't redefine any reserved words...
			if (!cursor->ScopeHash() && r) // only for global types(int, char, ...) not types in templates
			{
				top = cursor;
				goto done;
			}
		}

		if (r < 0 && (curType == VaMacroDefArg || VaMacroDefNoArg == curType))
			continue; // macro text, not the definition

		if (r && cursor->IsConstructor())
		{
			if (r != -1 && !fds->scope /* && !fds->getConstructor*/)
			{
				// never return constructors here...
				continue;
			}
			r = -1999; // hide constructors w/in classes w/o base classes, yet above wildcard matches
		}

		if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
			continue;

		if (!guess)
			guess = cursor;

		if (r && rnk < r)
		{
			BOOL found = (!g_doDBCase) ? (cursor->SymScope().FindNoCase(fds->sym->c_str()) != -1)
			                           : (cursor->SymScope_sv().first.contains(*fds->sym));
			if (found)
			{
				rnk = r;
				top = cursor;
			}
		}
		else if (top && rnk == r && r != -2001 && concatCount < 10)
		{
			if (cursor->IsVaStdAfx())
			{
				// [case: 141427]
				// vastdafx items have priority over others -- change top to curent item
				auto [topDef, topDef_lock] = top->Def_sv();
				if (!topDef.empty())
				{
					auto [curDef, curDef_lock] = cursor->Def_sv();
					if (!ContainsField(curDef.data(), topDef.data())) // both string_views will be null-terminated
					{
						// VaStdAfx def should always be first, but append def of the previous item
						newDef = {curDef, kFormfeed, topDef};

						topDef_lock.release();
						curDef_lock.release();
						cursor->SetDef(newDef);
					}
				}

				top = cursor;
				rnk = r;
				continue;
			}

			if (fds->findFlag & FDF_NoConcat)
				continue;

			if (vaHashtag == curType)
				continue; // [case: 105593]

			// member same scope as prev, prepend def and remove ref
			DType* fnd;
			if (cursor->SymScope_sv().first.find(*fds->sym) != -1)
				fnd = cursor;
			else
				fnd = nullptr;
#if !defined(SEAN)
			try
#endif // !SEAN
			{
				if (fnd && top != fnd)
				{
					AutoLockCs l(sConcatLock);
					auto [topDef, topDef_lock] = top->Def_sv();
					if (topDef.length() < kMaxDefConcatLen)
					{
						// see if top def already contains this one
						auto [fndDef, fndDef_lock] = fnd->Def_sv();
						WTString fndDef2;
						if (fndDef.contains('\f'))
						{
							// Only the first should have multiple defs
							fndDef2 = TokenGetField2(fndDef, kFormfeed_ch);
							fndDef_lock.release();
							fnd->SetDef(fndDef2);
							fndDef = fndDef2;
						}

						if (!ContainsField(topDef.data(), fndDef.data())) // append if not already there; both string_views will be null-terminated
						{
							if (top->IsVaStdAfx())
							{
								// [case: 141427]
								// VaStdAfx def should always be first
								newDef = {topDef, kFormfeed, fndDef};
							}
							else if (cursor->IsPreferredDef())
								newDef = {fndDef, kFormfeed, topDef};
							else
								newDef = {topDef, kFormfeed, fndDef};

							topDef_lock.release();
							fndDef_lock.release();
							top->SetDef(newDef);
							concatCount++; //   just in case there are hundred of definitions for foo
							if (concatCount >= 10 && g_loggingEnabled)
							{
								vLog("FindFDS: concatcount %s", top->SymScope().c_str());
								_ASSERT("FindFDS: concatCount exceeds 10");
							}
						}
					}
					else
					{
						// [case: 20327] sanity check - prevent hang caused by growing top->Def() to over 120KB
						vLog("WARN: FindFDS: concat def B len(%u) cnt(%d) %s", (uint32_t)topDef.length(), concatCount,
						     top->SymScope().c_str());
					}
				}
			}
#if !defined(SEAN)
			catch (...)
			{
				VALOGEXCEPTION("FDC:F:");
				ASSERT(FALSE);
				Log("ERROR: FD::Find exception caught");
			}
#endif // !SEAN
		}
	}

done:
	if (!top && guess && fds->findFlag & FDF_GUESS)
	{
		top = guess;
	}
	if (top)
	{
		fds->record = top;
		fds->scoperank = rnk;
	}

	if (fds->record)
		fds->record->LoadStrs();
	return fds->record;
}
DType* FileDic::Find(FindData* fds)
{
	// During large 'TOptional' findref in UE5:
	//   - Find is called 4.7mil times
	//   - rows are iterated 3.6bil times:
	//     - 4% is skipped by symbol hash check
	//     - 3% is skipped by IS_OBJECT_TYPE check
	//     - 93% comes to the end of the 'for'
	//     - 91% iterations has r = scopeArray.Contains -> 0
	//   - 100% calls to Find has g_doDBCase == true
	//   - 0.2% calls to Find has kUseNewNamespaceLogic == true
	//   - scopeArray has 37 elements on average
	// The idea is to go through all combinations of symbol hash combined with all scope hashes from scopeArray.
	// If scope hash is not present in the scopeArray, 'r' will be 0.
	//   - rnk is modified only when r is not 0
	//   - rnk comes from fds->scoperank which i don't think it can be zero; starts with -9999 and gets updated with non-zero r
	//   - 'guess' is used if 'fds->findFlag & FDF_GUESS' is specified, so I won't optimize in that case (rare)
	// If r is zero, meaning scope hash is not present in the scopeArray, iteration will come to the end of the 'for' and
	// continue to the next one.

	if (GetHashTable()->HasScopeSymbolIndex() && g_doDBCase && !fds->IsUsingNewNamespaceLogic() && !(fds->findFlag & FDF_GUESS) && (fds->scopeArray.GetCount() > 0))
	{
		// 		static int cnt;
		// 		++cnt;
		auto ret = FindImpl<true>(fds);
#ifdef _DEBUG
		auto old = FindImpl<false>(fds);
		assert(ret == old);
#endif
		return ret;
	}
	else
		return FindImpl<false>(fds);
}

void FileDic::GetMembersList(const WTString& scope, WTString& baseclasslist, ExpansionData* lb)
{
	LOG((WTString("ScanLst ") + scope).c_str());
	LogElapsedTime let("FD::GML 1", 100);
	DTypeList dtypes;
	GetMembersList(scope, baseclasslist, dtypes, true);
	int cnt = lb->GetCount();
	for (DTypeList::iterator it = dtypes.begin(); it != dtypes.end(); ++it)
	{
		DType& cur = *it;
		WTString s = cur.SymScope();
		s = s.substr(s.ReverseFind(DB_SEP_CHR) + 1);
		if (cur.Def().Find("operator") != -1 && s.find("operator") == -1)
			s.prepend("operator ");
		if (s.IsEmpty())
			continue;

		if (++cnt < MAXFILTERCOUNT)
		{
			// [case: 66420]
			lb->AddStringAndSort(s, cur.MaskedType(), cur.Attributes(), cur.SymHash(), cur.ScopeHash());
		}
		else
		{
			// [case: 66420]
			vLog("FD::GML: truncate threshold reached, cnt(%zu)\n", dtypes.size());
			break;
			// instead of breaking, we could do:
			//		lb->AddStringNoSort(s, cur.MaskedType(), cur.Attributes(), cur.SymHash(), cur.ScopeHash());
			// but filtering has no effect when list > MAXFILTERCOUNT, so there's no
			// point in adding everything that we can; the only benefit is that the user
			// could scroll through the list - but filtering by typing won't work
		}
	}
}

void FileDic::GetMembersList(const WTString& scope, WTString& baseclasslist, DTypeList& dlist,
                             bool filterBaseOverrides /*= false*/)
{
	LogElapsedTime let("FD::GML 2", 100);
	DB_READ_LOCK;
	////////////////////////////////////////////////////
	// Scan all rows finding each one with matching scope
	LOG((WTString("ScanLst2 ") + scope).c_str());
	bool bcWild = false;
	if (baseclasslist.contains(WILD_CARD_SCOPE))
	{
		// don't expand every member of every class for T::
		token t = baseclasslist;
		t.ReplaceAll(WILD_CARD_SCOPE, "");
		baseclasslist = t.Str();
		bcWild = true;
	}

	if (baseclasslist.length() < 2)
		return;

	CLEARERRNO;
	const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();
	CSpinCriticalSection listLock;
	ScopeHashAry curscope(scope.length() ? scope.c_str() : NULL, baseclasslist, NULLSTR);
	const uint sz = m_pHashTable->TableSize();
	auto reader = [&](uint r) {
		for (DefObj* cursor = m_pHashTable->GetRowHead(r); cursor; cursor = cursor->next)
		{
			const uint cursorScopeHash = cursor->ScopeHash();
			if (!cursorScopeHash)
				continue;

			const int rank = curscope.Contains(cursorScopeHash);
			if (!rank)
				continue;

			if (RESWORD == cursor->MaskedType())
				continue;

			if (cursor->IsHideFromUser())
				continue;

			if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
				continue;

			cursor->LoadStrs();
			_ASSERTE(!cursor->Def().contains("HIDETHIS"));
			AutoLockCs l(listLock);
			bool doAdd = true;
			if (filterBaseOverrides && !bcWild && rank < 0 && rank > -1000 && dlist.size() < 1000) // [case: 101940]
			{
				// [case: 98621]
				// check for overrides of base class
				// do not add members of base classes that are overridden in derived classes
				for (auto& it : dlist)
				{
					if (it.SymHash() == cursor->SymHash() && it.ScopeHash() != cursor->ScopeHash())
					{
						// pick a winner
						const int itRank = curscope.Contains(it.ScopeHash());
						if (itRank < 0 && itRank > -1000)
						{
							if (itRank < rank)
							{
								// loser already in list, remove it and allow cursor add
								dlist.remove(it);
							}
							else
							{
								// skip cursor since it is loser
								doAdd = false;
							}
						}

						break;
					}
				}
			}

			if (doAdd)
				dlist.push_back(cursor);
		}
	};

#pragma warning(push)
#pragma warning(disable : 4127)
	if (true || Psettings->mUsePpl)
		Concurrency::parallel_for((uint)0, sz, reader);
	else
		::serial_for<uint>((uint)0, sz, reader);
#pragma warning(pop)
}

// Typing wm_, get the first n entries, even though only 5 will be shown
// Latter suggestions will pull from this list so typing sw_act will offer SW_SHOWNOACTIVATE
#define GUESSCOUNT 100

// FileDic::GetHint
// ----------------------------------------------------------------------------
// TODO: this looks like it requires a DatabaseDirectoryLock but it is called
// on the UI thread...
//
bool FileDic::GetHint(const WTString sym, const WTString scope, WTString& baselist, WTString& hintstr)
{
	//////////////////////////////////////////////////////
	// look in sorted file for string that begins with sym
	LOG((WTString("GetHint ") + sym + "\n").c_str());
	DEFTIMER(GetHintTimer);
	BOOL doCase = FALSE;
	for (LPCSTR pp = sym.c_str(); !doCase && *pp; pp++)
		doCase = wt_isupper(*pp);
	if (!sym.length() || !ISCSYM(sym[0]))
		return false;

	WTString s, lsts, orgsym = sym, last;
	ScopeHashAry curscope(scope.length() ? scope.c_str() : NULL, baselist, NULLSTR);

	{
		AutoLockCs l(mHintStringsLock);
		if (!_tcsnicmp(m_lastuniqsymHint.c_str(), sym.c_str(), (size_t)sym.GetLength()) &&
		    !_tcsnicmp(m_lastkeyHint.c_str(), sym.c_str(), (size_t)m_lastkeyHint.GetLength()))
		{
			hintstr = m_lastuniqsymHint;
			return true;
		}
		m_lastuniqsymHint.Empty();
	}
	WTString nonUsedGuesses;

	MSG msg;
	uint scopeHash;
#ifdef _DEBUG
	int count = 0;
#endif

	/////////////////////////////////////
	// Search sorted file
	const CStringW dbDir(GetDBDir());
	const CStringW symW(sym.Wide());
	_ASSERTE(!dbDir.IsEmpty());
	if (::IsFile(LETTER_FILE(dbDir, symW[0])))
	{
#if _MSC_VER <= 1200
		ifstream ifs(LETTER_FILE(dbDir, symW[0]), ios::nocreate | ios::in | ios::binary);
#else
		ifstream ifs(LETTER_FILE(dbDir, symW[0]), ios::_Nocreate | ios::in | ios::binary);
#endif
		if (ifs && sym.length() > 1)
		{
			LOG("fseek");
			// binary search into sorted datafile...
			// CWnd was kinda slow cause it falls at end of file
			ifs.seekg(0, ios::end);
			ifstream::pos_type p1 = 0, p2 = ifs.tellg();
			WTString ln;
			for (int n = 0; (p2 - p1) > 1500; n++)
			{
				ifstream::pos_type half = (p1 + (p2 - p1) / 2);
				ifs.seekg(half);
				ln.read_line(ifs);
				ln.read_to_delim(ifs, DB_FIELD_DELIMITER);
				if (_tcsnicmp(ln.c_str(), sym.c_str(), (size_t)sym.length()) < 0)
					p1 = half;
				else
					p2 = half;
			}
			ifs.seekg(p1);
			if (p1)
				ln.read_line(ifs);
		}

		while (ifs && g_Guesses.GetCount() < (GUESSCOUNT))
		{
#ifdef _DEBUG
			count++;
#endif
			if (PeekMessage(&msg, GetFocus(), WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE))
				return false;

			while (ifs && wt_isspace(ifs.peek()))
				ifs.get();
			s.read_to_delim(ifs, DB_FIELD_DELIMITER);
			ifs >> scopeHash;
			int i = _tcsnicmp(sym.c_str(), s.c_str(), (size_t)sym.length());
			//			int i = ContainsSubset(s, sym, 0x2); // too slow for suggestions
			if (i == 0)
			{
				// append to list
				if (s != lsts)
				{
					// test if sym is in scope
					int i2 = curscope.Contains(scopeHash);
					if (i2)
					{
						// verify sym is still valid
						DWORD symhash = WTHashKey(s);
						DB_READ_LOCK;
						DefObj* cursor = m_pHashTable->FindRowHead(symhash);
						while (cursor && (cursor->ScopeHash() != (int)scopeHash || cursor->SymHash() != (int)symhash ||
						                  cursor->MaskedType() == COMMENT ||
						                  cursor->MaskedType() == GOTODEF)) // filter out bogus symbols
						{
							cursor = cursor->next;
						}

						if (cursor)
						{
							if ((!doCase || strncmp(s.c_str(), sym.c_str(), strlen(sym.c_str())) == 0))
							{
								const WTString entry(CompletionSetEntry(s.c_str()));
								AutoLockCs l(g_Guesses.GetLock());
								if (!g_Guesses.Contains(entry))
								{
									g_Guesses.AddGuess(entry);
									if (g_Guesses.GetCount() > (GUESSCOUNT))
										return TRUE;
								}
							}
						}
					}
				}
			}
			else if (i < 0)
				break;

			if (ifs)
			{
				WTString tmp;
				tmp.read_to_delim(ifs, '\n');
			}

			if (!ifs && !ifs.eof())
				ifs.clear();
		}

		ifs.close();
	}

	//////////////////////////////////////
	// Search unsorted file
	BOOL lookInCache = FALSE;
again:
	CStringW lfile = lookInCache ? LETTER_FILE_UNSORTED(VaDirs::GetDbDir() + kDbIdDir[DbTypeId_ExternalOther] + L"\\",
	                                                    CStringW(symW[0]))
	                             : LETTER_FILE_UNSORTED(dbDir, CStringW(symW[0]));
	if (IsFile(lfile))
	{
		// insert unsorted items
		ifstream ifs(lfile);
		while (ifs && wt_isspace(ifs.peek()))
			ifs.get();

		while (ifs)
		{
#ifdef _DEBUG
			count++;
#endif
			if (PeekMessage(&msg, GetFocus(), WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE))
				return false;

			s.read_to_delim(ifs, DB_FIELD_DELIMITER);
			ifs >> scopeHash;

			const int i = _tcsnicmp(sym.c_str(), s.c_str(), (size_t)sym.length());
			if (i == 0 && curscope.Contains(scopeHash))
			{
				if (!doCase || strncmp(s.c_str(), sym.c_str(), strlen(sym.c_str())) == 0)
				{
					if (FindExact(s, scopeHash))
					{
						const WTString entry(CompletionSetEntry(s.c_str()));
						AutoLockCs l(g_Guesses.GetLock());
						if (!g_Guesses.Contains(entry))
						{
							g_Guesses.AddGuess(entry);
							if (g_Guesses.GetCount() > (GUESSCOUNT))
								return TRUE;
						}
					}
					//					else
					//					{
					//#ifdef _DEBUG
					//						_asm nop;
					//#endif // _DEBUG
					//					}
				}
			}

			if (ifs)
			{
				WTString tmp;
				tmp.read_to_delim(ifs, '\n');
			}

			if (!ifs && !ifs.eof())
				ifs.clear();
		}
	}
	if (!lookInCache && !m_isSysDic)
	{
		lookInCache = TRUE;
		goto again;
	}
#ifdef _DEBUG
	vLog("End GetHint, read: %d", count);
#endif
	///////////////////////////////////////////

	{
		AutoLockCs l(mHintStringsLock);
		m_lastkeyHint = sym;
		m_lastuniqsymHint = hintstr;
	}

	token t = nonUsedGuesses;
	AutoLockCs l(g_Guesses.GetLock());
	while (t.more() && g_Guesses.GetCount() < (GUESSCOUNT))
		g_Guesses.AddGuess(t.read("\002") + "\002");
	return true;
}

// FileDic::GetHint
// ----------------------------------------------------------------------------
// TODO: this looks like it requires a DatabaseDirectoryLock but it is called
// on the UI thread...
//
void FileDic::GetCompletionList(const WTString& sym, const WTString& scope, WTString& baselist, ExpansionData* lstbox,
                                bool exact)
{
	DB_READ_LOCK;
	//////////////////////////////////////////////////////
	// look in sorted file for string that begins with sym
	LOG((WTString("QryLst ") + sym + "\n").c_str());
	const CStringW dbDir(GetDBDir());
	const CStringW symW(sym.Wide());
	_ASSERTE(!dbDir.IsEmpty());
#if _MSC_VER <= 1200
	ifstream ifs(LETTER_FILE(dbDir, symW[0]), ios::nocreate | ios::in | ios::binary);
#else
	ifstream ifs(LETTER_FILE(dbDir, symW[0]), ios::_Nocreate | ios::in | ios::binary);
#endif

	WTString s, lsts, orgsym = sym;
	ScopeHashAry curscope(scope.length() ? scope.c_str() : NULL, baselist, NULLSTR);
	uint scopeHash, lastScopeHash = NPOS;
	int loopCount = 0, itemsFound = 0;
	/////////////////////////////////////
	// Search sorted file
	if (exact && ifs && sym.length() > 1)
	{
		LOG("fseek");
		// binary search into sorted datafile...
		// CWnd was kinda slow cause it falls at end of file
		ifs.seekg(0, ios::end);
		//		while(ifs && wt_isspace(ifs.peek())) ifs.get();
		ifstream::pos_type p1 = 0, p2 = ifs.tellg();
		WTString ln;
		for (int n = 0; (p2 - p1) > 1500; n++)
		{
			ifstream::pos_type half = (p1 + (p2 - p1) / 2);
			ifs.seekg(half);
			ln.read_line(ifs);
			ln.read_to_delim(ifs, DB_FIELD_DELIMITER);
			if (_tcsnicmp(ln.c_str(), sym.c_str(), (size_t)sym.length()) < 0)
				p1 = half;
			else
				p2 = half;
		}
		ifs.seekg(p1);
		if (p1)
			ln.read_line(ifs);
	}
	UINT vflag = Psettings->m_bAllowShorthand ? 0x2 : 0u;
	while (ifs)
	{
		++loopCount;

		while (ifs && wt_isspace(ifs.peek()))
			ifs.get();
		s.read_to_delim(ifs, DB_FIELD_DELIMITER);
		ifs >> scopeHash;
		int ContainsSubset(LPCSTR text, LPCSTR subText, UINT flag);
		//		int i = _tcsnicmp(sym, s, sym.length());
		int i = exact ? (_tcsnicmp(sym.c_str(), s.c_str(), (size_t)sym.length()) == 0)
		              : ContainsSubset(s.c_str(), sym.c_str(), vflag);
		if (i /*&& i < 5*/)
		{
			if (exact && s.length() != sym.length())
				return;
			// sym changed???
			if (sym != orgsym)
			{
				lstbox->Clear();
				GetCompletionList(sym, scope, baselist, lstbox, exact);
				return;
			}
			// append to list
			if (s != lsts || (s == lsts && scopeHash != lastScopeHash))
			{
				// test if sym is in scope
				int rank = curscope.Contains(scopeHash);
#ifdef jer
				if (!rank && (gTypingDevLang == CS || Is_VB_VBS_File(gTypingDevLang)))
				{
					// Display classes from all namespaces
					//  when the user hits Ctrl+Space in C#/VB
					rank = -999;
				}
#endif // jer
				if (/*1||*/ rank)
				{
					ifs.get();
					uint type;
					ifs >> type;
					_ASSERTE((type & VA_DB_FLAGS_MASK) == (type & TYPEMASK));
					type &= TYPEMASK;
					ifs.get();
					uint attrs;
					ifs >> attrs;
					// case=20683 file entries aren't completion items
					if (!(attrs & V_HIDEFROMUSER))
					{
						lstbox->AddStringAndSort(s, type, attrs, WTHashKey(s), scopeHash);
						if (itemsFound++ > (1024 * 2) && !(itemsFound % 1024))
						{
							if (::EdPeekMessage(g_currentEdCnt->GetSafeHwnd()))
								return;
						}
						lsts = s;
						lastScopeHash = scopeHash;
					}
				}
			}
		}
		else if (i < 0)
			break;
		else
		{
			if (loopCount > (1024 * 10) && !(loopCount % (1024 * 4)))
			{
				if (::EdPeekMessage(g_currentEdCnt->GetSafeHwnd()))
					return;
			}
		}

		if (ifs)
		{
			WTString tmp;
			tmp.read_to_delim(ifs, '\n');
		}

		if (!ifs && !ifs.eof())
			ifs.clear();
	}
	//////////////////////////////////////
	// Search unsorted file
	ifs.close();
	BOOL lookInCache = FALSE;
again:
	CStringW lfile = lookInCache ? LETTER_FILE_UNSORTED(VaDirs::GetDbDir() + kDbIdDir[DbTypeId_ExternalOther] + L"\\",
	                                                    CStringW(symW[0]))
	                             : LETTER_FILE_UNSORTED(dbDir, CStringW(symW[0]));

	if (IsFile(lfile))
	{
		// insert unsorted items
		ifstream ifs2(lfile);
		while (ifs2 && wt_isspace(ifs2.peek()))
			ifs2.get();

		while (ifs2)
		{
			++loopCount;
			s.read_to_delim(ifs2, DB_FIELD_DELIMITER);
			ifs2 >> scopeHash;

			// sym changed???
			if (sym != orgsym)
			{
				lstbox->Clear();
				GetCompletionList(sym, scope, baselist, lstbox, exact);
				return;
			}

			//			int i = strnicmp(sym, s, sym.length());
			int ContainsSubset(LPCSTR text, LPCSTR subText, UINT flag);
			int i = ContainsSubset(s.c_str(), sym.c_str(), vflag);
			if (i != 0)
			{
				// append to list
				if (!exact || s.length() == sym.length())
				{
					// add to list
					int rank = curscope.Contains(scopeHash);
					if (/*1 ||*/ rank)
					{
						// we assume that the list is full from the sorted file,
						// so reading this from file should be quicker than scanning list?
						ifs2.get();
						uint type, attrs;
						ifs2 >> type;
						_ASSERTE((type & VA_DB_FLAGS_MASK) == (type & TYPEMASK));
						type &= TYPEMASK;
						ifs2.get();
						ifs2 >> attrs;

						// verify sym is still valid
						DWORD symhash = WTHashKey(s);
						DefObj* cursor = m_pHashTable->FindRowHead(symhash);
						while (cursor && (cursor->ScopeHash() != (int)scopeHash || cursor->SymHash() != (int)symhash ||
						                  cursor->MaskedType() == COMMENT ||
						                  cursor->MaskedType() == GOTODEF)) // filter out bogus symbols
						{
							cursor = cursor->next;
						}
						if (FindExact(s, scopeHash))
						{
							if (cursor /*&& type != RESWORD*/)
							{
								lstbox->AddStringAndSort(s, type, attrs, symhash, scopeHash);
							}
						}
						//#ifdef _DEBUG
						//						else
						//						{
						//							int i = 123;
						//						}
						//#endif // _DEBUG
					}
				}
			}

			if (ifs2)
			{
				WTString tmp;
				tmp.read_to_delim(ifs2, '\n');
			}

			if (!ifs2 && !ifs2.eof())
				ifs2.clear();
		}
	}

	if (!lookInCache && !m_isSysDic)
	{
		lookInCache = TRUE;
		goto again;
	}
#ifdef _DEBUG
	vLog("End QryLst, read: %d", loopCount);
#endif
	///////////////////////////////////////////
}

void FileDic::add(const WTString& sym, const WTString& def, uint type, uint attrs, uint dbFlags, UINT fileID, int line)
{
	_ASSERTE((type & TYPEMASK) == type);
	_ASSERTE((dbFlags & VA_DB_FLAGS) == dbFlags);

	if (dbFlags & VA_DB_BackedByDatafile)
	{
		// assume not a VA_DB_BackedByDatafile entry since sym and def are being passed in
		dbFlags ^= VA_DB_BackedByDatafile;
	}

	// don't need to set modified since that is a flag for file db
	// modification, not hashtable modification

	DB_READ_LOCK;
	m_pHashTable->CreateEntry(DType(sym, def, type, attrs, dbFlags, line, fileID));
}

//////////////////////////////////////////
// simplified lock
// called to lock while adding records to db to prevent
// others from concatenating

int DatabaseDirectoryLock::sDirectoryLockCount = 0;
CCriticalSection DatabaseDirectoryLock::sDirectoryCs;

DatabaseDirectoryLock::DatabaseDirectoryLock()
{
	SetupLock();
}

void DatabaseDirectoryLock::SetupLock()
{
	LOG("LockDirectory+");
	DEFTIMER(FileLockTimer1);
	sDirectoryCs.Lock();
	// do not use lock on UI thread unless shutting down
#if !defined(VA_CPPUNIT)
	ASSERT_ONCE(gShellIsUnloading || g_mainThread != ::GetCurrentThreadId() ||
	            (g_mainThread == ::GetCurrentThreadId() && RefactoringActive::IsActive()));
#endif // VA_CPPUNIT
	_ASSERTE(!GetOwningThreadID() || ::GetCurrentThreadId() == GetOwningThreadID());

	if (!sDirectoryLockCount++)
		InterlockedIncrement(&g_threadCount);
}

DatabaseDirectoryLock::~DatabaseDirectoryLock()
{
	LOG("LockDirectory-");
	DEFTIMER(FileLockTimer2);
	_ASSERTE(sDirectoryLockCount > 0);
	if (1 == sDirectoryLockCount)
	{
		g_DBFiles.Save(); // needed? $$MSD 2008.05.21
		_ASSERTE(g_threadCount != 0);
		InterlockedDecrement(&g_threadCount);
	}

	// decrement after all the cleanup so that lock asserts can be used
	sDirectoryLockCount--;
	sDirectoryCs.Unlock();
}

void FileDic::Flush()
{
	LOG3("FileDic::Flush");
	DEFTIMER(FlushTimer);
	m_loaded = false;
	SortIndexes();
	{
		_ASSERTE(DatabaseDirectoryLock::GetLockCount());
		DB_WRITE_LOCK;
		m_pHashTable->Flush();
	}
	m_modified = 0;
	SetStatusQueued(IDS_READY);
}

extern char EncodeChar(TCHAR c);

// given a sym ":foo:bar" it will find <the first> def and return the file and line it come from
void FileDic::FindDefList(WTString sym, bool matchScope, SymbolPosList& posList, bool matchBcl /*= false*/,
                          bool scopeInParens /*= false*/)
{
	// matchScope and matchBcl are mutually exclusive
	_ASSERTE((!matchBcl && !matchScope) || (matchBcl && !matchScope) || (matchScope && !matchBcl));
	DEFTIMERNOTE(FindDefList, sym);
	DB_READ_LOCK;

	MultiParsePtr mp;
	WTString symScopeForBclCompare;
	if (matchBcl)
	{
		symScopeForBclCompare = ::StrGetSymScope(sym);
		if (symScopeForBclCompare.IsEmpty())
			return;

		symScopeForBclCompare += "\f";
		mp = MultiParse::Create(gTypingDevLang);
	}

	CStringW file;
	int maxFound = kMaxDefListFindCnt; // One user had a method overridden 300+ times, and hence goto had 600+ entries
	                                   // of def and impl's.
	const bool kIsHashtag = sym.GetLength() >= 2 && sym[1] == '#';
	// Find all instances, regardless of scope in comments/strings. case=17089
	_ASSERTE(matchScope || StrGetSymScope(sym) == "" || matchBcl);
	if (!matchScope && !matchBcl)
	{
		// hack for this to be here: don't limit number of results for hashtags
		if (!kIsHashtag)
			maxFound = kUnmatchedScopeMaxFindCnt; // lower limit if not matching scope
	}

	const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();
	const auto doDBCase = g_doDBCase;
	UseHashEqualAC_local_fast;
	const uint tSymHash = DType::GetSymHash(sym);
	const uint tScopeHash = DType::GetScopeHash(sym);

	for (DefObj* cursor = m_pHashTable->FindRowHead(tSymHash); cursor && maxFound > 0; cursor = cursor->next)
	{
		if (!(HashEqualAC_local_fast(tSymHash, cursor->SymHash())))
			continue;

		if (matchScope && tScopeHash != cursor->ScopeHash())
			continue;

		if (!cursor->FileId())
			continue;

		file = gFileIdManager->GetFileForUser(cursor->FileId());
		if (file.IsEmpty())
			continue;

		if (matchBcl)
		{
			BaseClassFinder bcf(gTypingDevLang);
			const WTString bcl(bcf.GetBaseClassList(mp, cursor->Scope(), false, NULL));
			// see if cursor bcl contains symScopeForBclCompare
			if (-1 == bcl.Find(symScopeForBclCompare.c_str()))
				continue;
		}

		if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
			continue;

		if (kIsHashtag)
		{
			if (cursor->MaskedType() != vaHashtag)
			{
				// [case: 113990] symHash collision
				continue;
			}
		}
		else if (cursor->MaskedType() == vaHashtag)
		{
			// [case: 113990] symHash collision
			continue;
		}

		WTString def(cursor->Def());

		// The first entry may have all defs listed, get only the first.
		TokenGetField2InPlace(def, '\f');
		const int kMaxDefLen = 150; // [case: 5189] increase max from 50 to 150
		if (def.GetLength() > kMaxDefLen)
		{
			CStringW tmp(def.Wide());
			tmp = tmp.Mid(0, kMaxDefLen - 3) + L"...";
			def = tmp;
		}

		CStringW cursorName(def.Wide());
		if (DB_SEP_CHR == cursorName[0])
			cursorName = cursorName.Mid(1);

		CStringW displayTxt(file + COLONSTR.Wide() + itosw(cursor->Line()) + L"\f   ");
		if (gTestLogger && gTestLogger->IsMenuLoggingEnabled() && gTestLogger->StripLineNumbers())
			displayTxt = file + COLONSTR.Wide() + L"(line num removed)" + L"\f   ";
		if (matchBcl || scopeInParens)
		{
			WTString curScp(cursor->Scope());
			if (curScp[0] == ':')
				curScp = curScp.Mid(1);
			if (IsCFile(gTypingDevLang))
				curScp.ReplaceAll(":", "::");
			else
				curScp.ReplaceAll(":", ".");

			if (-1 == cursorName.Find(curScp.Wide()))
				displayTxt += L"(" + curScp.Wide() + L") ";
		}

		displayTxt = displayTxt + cursorName;

		if ((cursor->IsMethod() || cursor->MaskedType() == GOTODEF) &&
		    (GetFileType(file) == Src || file.Find(L".inl") != -1))
			// put implementations at the top of the list
			posList.AddHead(SymbolPositionInfo(file, cursor->Line(), displayTxt, cursor->MaskedType(),
			                                   cursor->Attributes(), cursor->DbFlags(), cursor->Def()));
		else
			posList.Add(SymbolPositionInfo(file, cursor->Line(), displayTxt, cursor->MaskedType(), cursor->Attributes(),
			                               cursor->DbFlags(), cursor->Def()));

		maxFound--;
	}
}

void Dic::FindDefList(WTString sym, bool matchScope, SymbolPosList& posList, bool matchBcl /*= false*/)
{
	// matchScope and matchBcl are mutually exclusive
	_ASSERTE((!matchBcl && !matchScope) || (matchBcl && !matchScope) || (matchScope && !matchBcl));
	DEFTIMERNOTE(FindDefList, sym);
	DB_READ_LOCK;

	MultiParsePtr mp;
	WTString symScopeForBclCompare;
	if (matchBcl)
	{
		symScopeForBclCompare = ::StrGetSymScope(sym);
		if (symScopeForBclCompare.IsEmpty())
			return;

		symScopeForBclCompare += "\f";
		mp = MultiParse::Create(gTypingDevLang);
	}

	CStringW file;
	int maxFound = kMaxDefListFindCnt; // One user had a method overridden 300+ times, and hence goto had 600+ entries
	                                   // of def and impl's.
	// Find all instances, regardless of scope in comments/strings. case=17089
	_ASSERTE(matchScope || StrGetSymScope(sym) == "" || matchBcl);
	if (!matchScope && !matchBcl)
	{
		// hack for this to be here: don't limit number of results for hashtags
		const bool isHashtag = sym.GetLength() >= 2 && sym[1] == '#';
		if (!isHashtag)
			maxFound = kUnmatchedScopeMaxFindCnt; // lower limit if not matching scope
	}

	const auto doDBCase = g_doDBCase;
	UseHashEqualAC_local_fast;
	const uint tSymHash = DType::GetSymHash(sym);
	const uint tScopeHash = DType::GetScopeHash(sym);

	for (DefObj* cursor = mHashTable->FindRowHead(tSymHash); cursor && maxFound > 0; cursor = cursor->next)
	{
		if (!(HashEqualAC_local_fast(tSymHash, cursor->SymHash())))
			continue;

		if (matchScope && tScopeHash != cursor->ScopeHash())
			continue;

		if (!cursor->FileId())
			continue;

		file = gFileIdManager->GetFileForUser(cursor->FileId());
		if (file.IsEmpty())
			continue;

		if (matchBcl)
		{
			BaseClassFinder bcf(gTypingDevLang);
			const WTString bcl(bcf.GetBaseClassList(mp, cursor->Scope(), false, NULL));
			// see if cursor bcl contains symScopeForBclCompare
			if (-1 == bcl.Find(symScopeForBclCompare.c_str()))
				continue;
		}

		// The first entry may have all defs listed, get only the first.
		WTString def = TokenGetField2(cursor->Def_sv().first, '\f');
		const int kMaxDefLen = 150; // [case: 5189] increase max from 50 to 150
		if (def.GetLength() > kMaxDefLen)
		{
			CStringW tmp(def.Wide());
			tmp = tmp.Mid(0, kMaxDefLen - 3) + L"...";
			def = tmp;
		}
		LPCSTR p = def.c_str();
		if (*p == DB_SEP_CHR)
			p++;

		const CStringW cursorName(WTString(p).Wide());
		CStringW displayTxt(file + COLONSTR.Wide() + itosw(cursor->Line()) + L"\f   ");
		if (matchBcl)
		{
			WTString curScp(cursor->Scope());
			if (curScp[0] == ':')
				curScp = curScp.Mid(1);
			if (IsCFile(gTypingDevLang))
				curScp.ReplaceAll(":", "::");
			else
				curScp.ReplaceAll(":", ".");

			if (-1 == cursorName.Find(curScp.Wide()))
				displayTxt += L"(" + curScp.Wide() + L") ";
		}
		displayTxt += cursorName;

		if ((cursor->IsMethod() || cursor->MaskedType() == GOTODEF) &&
		    (GetFileType(file) == Src || file.Find(L".inl") != -1))
		{
			// put implementations at the top of the list
			cursor->LoadStrs();
			posList.AddHead(SymbolPositionInfo(file, cursor->Line(), displayTxt, cursor->MaskedType(),
			                                   cursor->Attributes(), cursor->DbFlags(), cursor->Def()));
		}
		else
		{
			cursor->LoadStrs();
			posList.Add(SymbolPositionInfo(file, cursor->Line(), displayTxt, cursor->MaskedType(), cursor->Attributes(),
			                               cursor->DbFlags(), cursor->Def()));
		}

		maxFound--;
	}
}

DType* Dic::FindMatch(DTypePtr symDat)
{
	_ASSERTE(symDat);
	DB_READ_LOCK;

	// find match in list
	for (DefObj* cursor = mHashTable->FindRowHead(symDat->SymHash()); cursor; cursor = cursor->next)
	{
		if (*cursor == *symDat.get())
			return cursor;
	}

	return nullptr;
}

// FileDic::RemoveAllDefsFrom
// ----------------------------------------------------------------------------
// Removes nodes from hashtable and removes entries (or invalidates) from dbfiles
// in a single pass
//
void FileDic::RemoveAllDefsFrom_SinglePass(CStringW file)
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	DEFTIMER(RemoveAllDefsFrom_SinglePassTimer);
	// add try/catch block so that write lock is always unlocked
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		file = MSPath(file);
		const UINT fileid = gFileIdManager->GetFileId(file);
		{
			DB_WRITE_LOCK; // wait for all other threads to release db
			CSpinCriticalSection tmplItemsLock;
			std::vector<DefObj*> templateItems;
			UINT count = 0;
			const uint sz = m_pHashTable->TableSize();

			VAHashTable::do_detach_locks_t do_detach_locks;
			auto remover = [&](uint curRowIdx) {
				if (StopIt)
					return;

				VAHashTable::Row* row = m_pHashTable->GetRow(curRowIdx);
				DefObj* prev = nullptr;
				DefObj* cursor = row ? row->Head() : nullptr;
				while (cursor)
				{
					InterlockedIncrement(&count);
					DefObj* next = cursor->next;
					if (cursor->FileId() == fileid)
					{
						if (cursor->IsTemplateItem())
						{
							{
								AutoLockCs l(tmplItemsLock);
								templateItems.push_back(cursor);
							}
							prev = cursor;
						}
						else
						{
							m_pHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
							_ASSERTE(!prev || prev->next == next);
						}
					}
					else
						prev = cursor;

					if (cursor == next)
						break;
					cursor = next;
				}
			};

#pragma warning(push)
#pragma warning(disable : 4127)
			if (true || Psettings->mUsePpl)
				Concurrency::parallel_for((uint)0, sz, remover);
			else
				::serial_for<uint>((uint)0, sz, remover);
#pragma warning(pop)

			// [case: 9603] remove stale template items if there aren't too many
			const size_t kTemplateItemCnt = templateItems.size();
			if (kTemplateItemCnt && kTemplateItemCnt < kMaxTemplateItemsThatCanBeRemoved)
			{
				for (std::vector<DefObj*>::iterator it = templateItems.begin(); it != templateItems.end(); ++it)
					m_pHashTable->DoDetach(*it);
			}
		}

		// remove from dbfiles
		// NOTE: this deletes from all dbFiles even though this (a FileDic)
		// is either project OR system
		g_DBFiles.RemoveFile(fileid);
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FD:");
		Log("ERROR: exception caught in FD::RemoveAllDefsFrom");
	}
#endif // !SEAN
}

// FileDic::RemoveAllDefsFrom
// ----------------------------------------------------------------------------
// Removes nodes from hashtable and invalidates entries in dbfiles in a single pass.
// Does not remove invalidated entries in dbfiles.
//
void FileDic::RemoveAllDefsFrom(const std::set<UINT>& fileIds)
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	DEFTIMER(RemoveAllDefsFrom_SinglePassTimer);
	// add try/catch block so that write lock is always unlocked
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		{
			DB_WRITE_LOCK; // wait for all other threads to release db
			CSpinCriticalSection tmplItemsLock;
			std::vector<DefObj*> templateItems;
			UINT count = 0;
			const uint sz = m_pHashTable->TableSize();

			VAHashTable::do_detach_locks_t do_detach_locks;
			auto remover = [&](uint curRowIdx) {
				if (StopIt)
					return;

				VAHashTable::Row* row = m_pHashTable->GetRow(curRowIdx);
				DefObj* prev = nullptr;
				DefObj* cursor = row ? row->Head() : nullptr;
				while (cursor)
				{
					InterlockedIncrement(&count);
					DefObj* next = cursor->next;
					if (fileIds.cend() != fileIds.find(cursor->FileId()))
					{
						if (cursor->IsTemplateItem())
						{
							{
								AutoLockCs l(tmplItemsLock);
								templateItems.push_back(cursor);
							}
							prev = cursor;
						}
						else
						{
							m_pHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
							_ASSERTE(!prev || prev->next == next);
						}
					}
					else
						prev = cursor;

					if (cursor == next)
						break;
					cursor = next;
				}
			};

#pragma warning(push)
#pragma warning(disable : 4127)
			if (true || Psettings->mUsePpl)
				Concurrency::parallel_for((uint)0, sz, remover);
			else
				::serial_for<uint>((uint)0, sz, remover);
#pragma warning(pop)

			// [case: 9603] remove stale template items if there aren't too many
			const size_t kTemplateItemCnt = templateItems.size();
			if (kTemplateItemCnt && kTemplateItemCnt < (kMaxTemplateItemsThatCanBeRemoved * 100))
			{
				for (std::vector<DefObj*>::iterator it = templateItems.begin(); it != templateItems.end(); ++it)
					m_pHashTable->DoDetach(*it);
			}
		}

		// invalidate for later purge
		std::for_each(fileIds.cbegin(), fileIds.cend(), [](UINT fileid) { g_DBFiles.InvalidateFile(fileid); });
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FD:");
		Log("ERROR: exception caught in FD::RemoveAllDefsFrom*");
	}
#endif // !SEAN
}

// FileDic::RemoveAllDefsFrom_InitialPass
// ----------------------------------------------------------------------------
// Marks nodes that need to be removed from hashtable but does not actually remove them
// Removes entries from dbfiles
// Call RemoveMarkedDefs to remove the marked hashtable items
//
void FileDic::RemoveAllDefsFrom_InitialPass(CStringW file)
{
	_ASSERTE(DatabaseDirectoryLock::GetLockCount());
	DEFTIMER(RemoveAllDefsFrom_InitialPassTimer);
	// add try/catch block so that write lock is always unlocked
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		file = MSPath(file);
		const UINT fileid = gFileIdManager->GetFileId(file);
		{
			DB_WRITE_LOCK;
			CSpinCriticalSection tmplItemsLock;
			std::vector<DefObj*> templateItems;
			UINT count = 0;
			const uint sz = m_pHashTable->TableSize();

			VAHashTable::do_detach_locks_t do_detach_locks;
			auto remover = [&](uint curRowIdx) {
				if (StopIt)
					return;

				VAHashTable::Row* row = m_pHashTable->GetRow(curRowIdx);
				DefObj* prev = nullptr;
				DefObj* cursor = row ? row->Head() : nullptr;
				while (cursor)
				{
					InterlockedIncrement(&count);
					DefObj* next = cursor->next;
					if (cursor->FileId() == fileid)
					{
						if (cursor->IsTemplateItem())
						{
							{
								AutoLockCs l(tmplItemsLock);
								templateItems.push_back(cursor);
							}
							prev = cursor;
						}
						else
						{
							if (cursor->SymHash() == (int)fileid && !cursor->ScopeHash())
							{
								m_pHashTable->DoDetachWithHint(row, cursor,
								                               prev, do_detach_locks); // the file data entry gets removed in the first pass
								_ASSERTE(!prev || prev->next == next);
							}
							else
							{
								cursor->MarkForDetach();
								prev = cursor;
							}
						}
					}
					else
						prev = cursor;

					if (cursor == next)
						break;
					cursor = next;
				}
			};

#pragma warning(push)
#pragma warning(disable : 4127)
			if (true || Psettings->mUsePpl)
				Concurrency::parallel_for((uint)0, sz, remover);
			else
				::serial_for<uint>((uint)0, sz, remover);
#pragma warning(pop)

			// [case: 9603] mark stale template items for removal if there aren't too many
			const size_t kTemplateItemCnt = templateItems.size();
			if (kTemplateItemCnt && kTemplateItemCnt < kMaxTemplateItemsThatCanBeRemoved)
			{
				for (std::vector<DefObj*>::iterator it = templateItems.begin(); it != templateItems.end(); ++it)
					(*it)->MarkForDetach();
			}
		}

		// remove from dbfiles
		// NOTE: this deletes from all dbFiles even though this (a FileDic)
		// is either project OR system
		g_DBFiles.RemoveFile(fileid);
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FD:");
		Log("ERROR: exception caught in FD::RemoveAllDefsFrom_InitialPass");
	}
#endif // !SEAN
}

// FileDic::RemoveMarkedDefs
// ----------------------------------------------------------------------------
// Removes nodes from hashtable that have been marked for removal
//
void FileDic::RemoveMarkedDefs()
{
	DB_WRITE_LOCK; // wait for all other threads to release db
	DEFTIMER(RemoveMarkedDefs_Timer);
	// add try/catch block so that write lock is always unlocked
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		const uint sz = m_pHashTable->TableSize();
		VAHashTable::do_detach_locks_t do_detach_locks;
		auto remover = [&](uint curRowIdx) {
			if (StopIt)
				return;

			VAHashTable::Row* row = m_pHashTable->GetRow(curRowIdx);
			DefObj* prev = nullptr;
			DefObj* cursor = row ? row->Head() : nullptr;
			while (cursor)
			{
				DefObj* next = cursor->next;
				if (cursor->IsMarkedForDetach())
				{
					m_pHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
					_ASSERTE(!prev || prev->next == next);
				}
				else
					prev = cursor;
				if (cursor == next)
					break;
				cursor = next;
			}
		};

#pragma warning(push)
#pragma warning(disable : 4127)
		if (true || Psettings->mUsePpl)
			Concurrency::parallel_for((uint)0, sz, remover);
		else
			::serial_for<uint>((uint)0, sz, remover);
#pragma warning(pop)
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FD:");
		Log("ERROR: exception caught in FD::RemoveMarkedDefs");
	}
#endif // !SEAN
}

void FileDic::ReleaseTransientData(bool honorCountLimit /*= true*/)
{
	LOG("ReleaseTransientData");
	const ULONG cnt = GetCount();
	if (cnt < 50000)
		if (honorCountLimit || !cnt)
			return;

	DB_WRITE_LOCK;
	LogElapsedTime let("FD::RTD", 500);
#if !defined(SEAN)
	try
#endif // !SEAN
	{
#ifdef _DEBUG
		SetStatus("Start ReleaseTransientData");
#endif
		long templateItemCount = 0;
		long bclCacheCount = 0;
		long vamacroCount = 0;
		long fileidCount = 0;

		const uint sz = m_pHashTable->TableSize();
		VAHashTable::do_detach_locks_t do_detach_locks;
		auto releaser = [&](uint r) {
			DefObj* next = nullptr, *prev = nullptr;
			VAHashTable::Row *row = m_pHashTable->GetRow(r);
			for (DefObj* cursor = row->Head(); cursor; cursor = next)
			{
				next = cursor->next;

#if !defined(SEAN)
				try
#endif // !SEAN
				{
					if (cursor->IsTemplateItem())
					{
						m_pHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
						InterlockedIncrement(&templateItemCount);
					}
					else if (cursor->type() == CachedBaseclassList)
					{
						m_pHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
						InterlockedIncrement(&bclCacheCount);
					}
					else if ((VaMacroDefArg == cursor->type() || VaMacroDefNoArg == cursor->type()) &&
					         !cursor->IsDbBackedByDataFile() && !(cursor->Attributes() & V_VA_STDAFX))
					{
						// assume that the macro entries that ARE backed by datafile
						// have been read in by now; see:#parserMacrosAreAddedTwiceHere
						m_pHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
						InterlockedIncrement(&vamacroCount);
					}
					else if (cursor->type() == UNDEF && cursor->HasFileFlag() && !cursor->IsDbBackedByDataFile() &&
					         !(cursor->Attributes() & V_IDX_FLAG_INCLUDE))
					{
						// fileid entries that are duped during parse (but not during load from cache)
						// see:#parseFileidNotDbBacked
						// revisit: see if there is a way to clear these right after load of the dbfile
						m_pHashTable->DoDetachWithHint(row, cursor, prev, do_detach_locks);
						InterlockedIncrement(&fileidCount);
					}
					else
						prev = cursor;
				}
#if !defined(SEAN)
				catch (...)
				{
					VALOGEXCEPTION("FD:RTD1:");
					Log("ERROR: exception caught in inner FD::ReleaseTransientData");
					_ASSERTE(!"FileDic::ReleaseTransientData exception 1");
				}
#endif // !SEAN
			}
		};

#pragma warning(push)
#pragma warning(disable : 4127)
		if (true || Psettings->mUsePpl)
			Concurrency::parallel_for((uint)0, sz, releaser);
		else
			::serial_for<uint>((uint)0, sz, releaser);
#pragma warning(pop)

		CString msg;
		CString__FormatA(msg, "End ReleaseTransientData: tmpl(%ld), bcl(%ld) vam(%ld) fid(%ld)", templateItemCount,
		                 bclCacheCount, vamacroCount, fileidCount);
		vLog("%s", (const char*)msg);
#ifdef _DEBUG
		SetStatus(msg);
#endif
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FDC:RTD2:");
		Log("ERROR: exception caught in FD::ReleaseTransientData");
		_ASSERTE(!"FileDic::ReleaseTransientData exception 2");
	}
#endif // !SEAN
}

void FileDic::MakeTemplate(const WTString& templateName, const WTString& instanceDeclaration,
                           const WTString& constTemplateFormalDeclarationArguments,
                           const WTString& constInstanceArguments, BOOL doMemberClasses /*= TRUE*/)
{
	DB_READ_LOCK;
	///////////////////////////
	// pass in ":foo" and ":foo<classes>
	// copy all ":foo:*" to ":foo<classes>*"
	LOG("MakeTemplate");
	DEFTIMER(MakeTemplateTimer);
	//	SetStatus(IDS_MAKING_TEMPLATE, DecodeScope(instanceDeclaration).c_str());
	vLog("FD::MTI: %d (%s) (%s) (%s) (%s)\n", doMemberClasses, templateName.c_str(), instanceDeclaration.c_str(),
	     constTemplateFormalDeclarationArguments.c_str(), constInstanceArguments.c_str());
	const uint hkey = WTHashKey(templateName);
	uint skey = WTHashKey(templateName.c_str() + 1);
	token2 templateFormalDeclarationArguments = constTemplateFormalDeclarationArguments;
	templateFormalDeclarationArguments.ReplaceAll("class", "", true);
	templateFormalDeclarationArguments.ReplaceAll("typename", "", true);

	uint classscope = 0;
	const int scopePos = templateName.ReverseFind(DB_SEP_CHR);
	if (scopePos > 0)
	{
		classscope = WTHashKey(templateName.left_sv((uint32_t)scopePos));
		skey = WTHashKey(templateName.mid_sv((uint32_t)scopePos + 1));
	}

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		const long kMaxMakes = 500;
		long count = 0;
		const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();
		std::unordered_set<const DefObj*> objs;
		std::unordered_set<const DefObj*> objs_symscope;
		objs.reserve(4096u);
		CSpinCriticalSection objsLock;
		static const char lt_encoded = EncodeChar('<');

		if(m_pHashTable->HasScopeIndex() && m_pHashTable->HasScopeSymbolIndex()) {
			auto start_row = m_pHashTable->GetScopeRowNum(hkey);
			Concurrency::parallel_for(0u, m_pHashTable->GetScopeEndRowNum(hkey) - start_row + 1, [start_row, checkInvalidSys, &objsLock, &objs, &objs_symscope, this, skey, classscope](uint r) {
				if(r > 0)
				{
					std::vector<const DefObj*> myobjs;
					// find cursor->ScopeHash() == hkey
					for (const DefObj *cursor = m_pHashTable->GetScopeRowHead(start_row + r - 1), *next_scope; cursor; cursor = next_scope)
					{
						next_scope = cursor->next_scope;
						PreFetchCacheLine(PF_NON_TEMPORAL_LEVEL_ALL, next_scope);

						if (cursor->SymScope_sv().first.contains(lt_encoded))
							continue; // Prevent recursion

						if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
							continue;

						myobjs.emplace_back(cursor);
					}

					if (!myobjs.empty())
					{
						AutoLockCs l(objsLock);
						objs.insert_range(myobjs);
					}
				}
				else
				{
					// find cursor->SymHash() == skey && cursor->ScopeHash() == classscope
					for (const DefObj *cursor = m_pHashTable->FindScopeSymbolHead(classscope, skey), *next; cursor; cursor = next)
					{
						next = VAHashTable::ScopeSymbolNodeToDefObj(cursor->node_scope_sym_index.next);
						PreFetchCacheLine(PF_NON_TEMPORAL_LEVEL_ALL, next);

						if (cursor->SymScope_sv().first.contains(lt_encoded))
							continue; // Prevent recursion

						if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
							continue;

						objs_symscope.insert(cursor);
					}
				}
			});

			objs.merge(objs_symscope);
		}
		else
		{
			// no scope index; optimized version
			const uint sz = m_pHashTable->TableSize();
			constexpr uint batch_size = 256;
			const uint batches = (sz + batch_size - 1) / batch_size;

			auto mktmp = [&, hkey, skey, classscope, sz](uint b) {
				if (StopIt)
					return;

				std::vector<DefObj*> myobjs;
				myobjs.reserve(128);

				for (uint r = b * batch_size; r < std::min((b + 1) * batch_size, sz); r++)
				{
					for (DefObj *cursor = m_pHashTable->GetRowHead(r), *next; cursor; cursor = next)
					{
						next = cursor->next;
						PreFetchCacheLine(PF_NON_TEMPORAL_LEVEL_ALL, next);

						bool proceed = cursor->ScopeHash() == hkey;
						proceed |= (cursor->SymHash() == skey) & (cursor->ScopeHash() == classscope);
						if (!proceed) [[likely]]
							continue;

						if (cursor->SymScope_sv().first.contains(lt_encoded))
							continue; // Prevent recursion

						if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
							continue;

						myobjs.emplace_back(cursor);
					}
				}

				if (!myobjs.empty())
				{
					AutoLockCs l(objsLock);
					objs.insert_range(myobjs);
				}
			};

			// this is a performance hotspot because it walks over every item in the hashtable (sln and sys)
			if (Psettings->mUsePpl)
				Concurrency::parallel_for((uint)0, batches, mktmp);
			else
				::serial_for<uint>((uint)0, batches, mktmp);
		}


		if (!objs.empty())
			SetStatusQueued(IDS_MAKING_TEMPLATE, DecodeScope(instanceDeclaration).c_str());

		WTString cursorKey;
		WTString newTemplateName;
		WTString sym;
		for (auto it = objs.begin(); !StopIt && it != objs.end(); ++it)
		{
			const DefObj* cursor = *it;
			cursorKey = cursor->SymScope_sv().first;
// 			if (cursorKey.Find(EncodeChar('<')) != -1)
// 				continue; // Prevent recursion

			// copy and replace
			const uint curType = cursor->MaskedType();
			if (doMemberClasses && (CLASS == curType || STRUCT == curType) && // don't recurse for typedefs (case=5298)
			    templateName != cursorKey)
			{
				// template<T> class foo{ class bar{ // change T in bar as well even though bar is not a template
				// cut down on duplicated calls... (case=5298)

				XXH64_state_t state;
				XXH64_reset(&state, (uintptr_t)this);
				XXH64_update(&state, (const xxh_u8*)templateFormalDeclarationArguments.WTString::c_str(), templateFormalDeclarationArguments.WTString::GetULength());
				XXH64_update(&state, (const xxh_u8*)cursorKey.c_str(), cursorKey.GetULength());
				XXH64_update(&state, (const xxh_u8*)constInstanceArguments.c_str(), constInstanceArguments.GetULength());
				XXH64_update(&state, (const xxh_u8*)instanceDeclaration.c_str(), instanceDeclaration.GetULength());
				uint64_t key = XXH64_digest(&state);
				auto [_, inserted] = sPreviousMap.insert(key);

				if (inserted)
				{
					newTemplateName = {instanceDeclaration, DB_SEP_STR, ::StrGetSym_sv(cursorKey)};

					// Pass doMemberClasses=FALSE to limit the the chance of recursion
					MakeTemplate(cursorKey, newTemplateName, templateFormalDeclarationArguments, constInstanceArguments, FALSE);
				}
			}

			sym = {instanceDeclaration, cursorKey.mid_sv(templateName.GetULength())};
			if (::StrGetSym_sv(sym) == kEmptyTemplate)
			{
				// do not replace original template < class T>
				continue;
			}

			// replace template placeholder T's with instance args in def
			BOOL didReplace = FALSE;
			bool toTypeIsPtr = false;
			token2 def = cursor->Def();
			def = def.read('\f'); // only first def

			if (FUNC == curType)
			{
				DTypeList lst;
				if (FindExactList(kEmptyTemplate.c_str(), WTHashKey(cursorKey), lst, 1))
				{
					DType& dt = lst.front();
					if (dt.SymScope_sv().first == std::initializer_list<std::string_view>{cursorKey, DB_SEP_STR, "< >"})
					{
						if (dt.Def_sv().first.contains('='))
						{
							// [case: 112697]
							// template method in template class with independent template declaration arguments
							// that have default arguments (hence the above check for '=')
							token2 functionTemplateFormalDeclarationArguments = dt.Def();
							functionTemplateFormalDeclarationArguments.ReplaceAll("class", "", true);
							functionTemplateFormalDeclarationArguments.ReplaceAll("typename", "", true);

							::SubstituteFunctionTemplateInstanceDefaults(
							    functionTemplateFormalDeclarationArguments.Str(),
							    templateFormalDeclarationArguments.Str(), def);
						}
					}
				}
			}

			::SubstituteTemplateInstanceDefText(templateFormalDeclarationArguments.Str(), constInstanceArguments, def,
			                                    toTypeIsPtr, didReplace);

			if (cursor->ScopeHash() == classscope)
			{
				auto _defStr = def.Str();
				std::string_view defStr = _defStr;
				if (!defStr.empty())
				{
					// http://msdn.microsoft.com/en-us/library/6w96b5h7.aspx
					if (defStr[0] == 'p')
					{
						// remove class access
						if (defStr.starts_with("private "))
							defStr.remove_prefix(8);
						else if (defStr.starts_with("public "))
							defStr.remove_prefix(7);
					}

					// remove ref or value keyword
					if (defStr.starts_with("ref "))
						defStr.remove_prefix(4);
					else if (defStr.starts_with("value "))
						defStr.remove_prefix(6);
				}

				if (defStr.starts_with("class ") || defStr.starts_with("struct "))
				{
					// class definition
					// remove original template name from this instance
					def.read(':'); // strip "class name : "
					// adding class here does funny things to baseclass searches
					// for example, if def is "class foo{...}", this changes def to "class "
					// need to look into this in further detail to see if really necessary
					def = (WTString("class ") + def.Str()).c_str();
					if (def.Str() == "class ")
					{
						if (defStr.contains("{...}"))
							def = "{...}";
						else
							def = "";
					}
				}
			}

			// flag generated entry as V_TEMPLATE_ITEM so that instantiated
			// templates are not removed from hashtable (until reload of solution).
			// This is done for performance reasons when editing heavily templated files.
			// see case=9603 for a side-effect.
			uint newAttrs = cursor->Attributes() | V_TEMPLATE_ITEM;

			// [case: 23934] update type if template param was a pointer
			if (toTypeIsPtr && !(newAttrs & V_POINTER))
				newAttrs |= V_POINTER;

			// If we did the replacement, mark as preferred so we see its def first
			// in the list to fix template classes with multiple definitions.
			//
			// template class could have multiple definitions, offer defs we did the
			// replacement on first, see Find below
			if (didReplace)
				newAttrs |= V_PREFERREDDEFINITION;
			else
				newAttrs = newAttrs & ~V_PREFERREDDEFINITION;

			uint dbFlags = cursor->DbFlags();
			if (dbFlags & VA_DB_BackedByDatafile)
			{
				// instantiated template items are not from db files
				dbFlags ^= VA_DB_BackedByDatafile;
			}

			// Save to project db so templates of one project don't affect other projects. (p4 change 5372)
			// Add original def as fileID so we can goto its def instead...
			bool skip = false;
			skip |= sym.IsEmpty();
			if (!skip)
			{
				XXH64_state_t state;
				XXH64_reset(&state, 0);
				XXH64_update(&state, (const xxh_u8*)def.WTString::c_str(), def.WTString::GetULength());
				XXH64_update(&state, (const xxh_u8*)sym.c_str(), sym.GetULength());
				auto fileid_line = std::make_pair(cursor->FileId(), cursor->Line());
				XXH64_update(&state, (const xxh_u8*)&fileid_line, sizeof(fileid_line));
				uint64_t key = XXH64_digest(&state);

				auto [_, inserted] = sTemplatesAlreadyAdded.insert(key);
				skip |= !inserted;
			}
			if (!skip)
			{
				g_pGlobDic->add(sym, def.Str(), cursor->MaskedType(), newAttrs, dbFlags, cursor->FileId(), cursor->Line());

				if (++count >= kMaxMakes)
					break;
			}
		}

		vLog("FD::MakeTemplate %ld", count);
		SetStatusQueued(IDS_READY);
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FDC:MT:");
		// shutting down?
		Log("ERROR: exception caught in FD::MakeTemplate");
	}
#endif // !SEAN
}

// See if sym is defined anywhere, for ASC
uint FileDic::FindAny(const WTString& sym)
{
	DB_READ_LOCK;

	// find match in list
	const int doDBCase = g_doDBCase;
	UseHashEqualAC_local_fast;
	uint rtype = 0;
	const uint tSymHash = DType::GetSymHash(sym);
	for (DefObj* cursor = m_pHashTable->FindRowHead(tSymHash); cursor; cursor = cursor->next)
	{
		if (!(HashEqualAC_local_fast(tSymHash, cursor->SymHash())))
			continue;

		const uint type = cursor->MaskedType();
		if (type == VaMacroDefArg || type == VaMacroDefNoArg || type == GOTODEF || type == COMMENT ||
		    type == CachedBaseclassList || type == TEMPLATE_DEDUCTION_GUIDE || cursor->IsConstructor())
		{
			continue;
		}

		// allow V_HIDEFROMUSER for forward declarations
		// [case: 141700] don't call IsFromInvalidSystemFile

		if (IS_OBJECT_TYPE(type))
			return type; // CLASS;

		if (!m_isSysDic && cursor->IsSysLib())
		{
			// fixes coloring issues, template are members are stored to Global dict.
		}
		else if (!rtype || !cursor->ScopeHash())
		{
			rtype = type;
		}
	}
	return rtype;
}

void SplitTemplateArgs(const WTString& templateFormalDeclarationArguments,
                       std::vector<WTString>& templateFormalDeclarationArgumentList)
{
	ArgsSplitter rtc(Src);
	rtc.ReadTo(templateFormalDeclarationArguments, ";{");

	// [case: 90371]
	if (-1 != templateFormalDeclarationArguments.Find("COMMA"))
	{
		// vs2012 shared_ptr template is created from a macro that uses COMMA for commas.
		// a better fix would be in the parser to actually handle the COMMA macro, but
		// because the shared_ptr problem is only in vs2012, not vs2013+
		for (auto& curArg : rtc.mArgs)
		{
			auto pStr = strstrWholeWord(curArg, "_COMMA");
			if (!pStr)
				pStr = strstrWholeWord(curArg, "COMMA");

			if (pStr && pStr != curArg.c_str())
			{
				WTString tmp = curArg.Left(ptr_sub__int(pStr, curArg.c_str()));
				tmp.Trim();
				if (!tmp.IsEmpty())
				{
					templateFormalDeclarationArgumentList.push_back(tmp);

					tmp = curArg.Mid(ptr_sub__int(pStr, curArg.c_str()));
					tmp.Trim();
					templateFormalDeclarationArgumentList.push_back(tmp);
				}
			}
			else
				templateFormalDeclarationArgumentList.push_back(curArg);
		}
	}
	else
		templateFormalDeclarationArgumentList.swap(rtc.mArgs);
}

struct ChangeItem
{
	WTString mTextMatch;
	int mMatchPos = 0;
	WTString mReplacementText;

	ChangeItem(const WTString& tm, int pos, const WTString& rep) : mTextMatch(tm), mMatchPos(pos), mReplacementText(rep)
	{
	}

	bool is_in_range(int pos) const
	{
		return (pos >= mMatchPos) && (pos < (mMatchPos + mTextMatch.length()));
	}
};

void SplitInstanceArgs(const WTString& constInstanceArguments, std::vector<WTString>& instanceArgumentList)
{
	// strip ns::cls<foo>, strip off "cls:" to get to args
	LPCSTR p = constInstanceArguments.c_str();
	while (*p && *p != '<')
		p++;

	ArgsSplitter rtc(Src);
	rtc.ReadTo(WTString(p), ";{");
	instanceArgumentList.swap(rtc.mArgs);
}

bool ApplyTextSubstitutions(std::vector<ChangeItem>& changes, WTString& defStr)
{
	// sort changes by offset
	std::sort(changes.begin(), changes.end(),
	          [](const ChangeItem& c1, const ChangeItem& c2) -> bool { return c1.mMatchPos < c2.mMatchPos; });

	for(auto it = changes.begin(); it != changes.end();)
	{
		bool erase_change = false;
		for (auto it2 = changes.begin(); it2 != it; ++it2)
		{
			// if we have templ<T, templ2<T>>, discard inside substitution
			if (it2->is_in_range(it->mMatchPos) && it2->is_in_range(it->mMatchPos + it->mTextMatch.length() - 1))
			{
				erase_change = true;
				break;
			}
		}
		if (erase_change)
			it = changes.erase(it);
		else
			++it;
	}

	bool didReplace = false;
	for (auto& cur : changes)
	{
		if (defStr.Mid(cur.mMatchPos, cur.mTextMatch.GetLength()) == cur.mTextMatch)
		{
			defStr.ReplaceAt(cur.mMatchPos, cur.mTextMatch.GetLength(), cur.mReplacementText.c_str());
			didReplace = true;
		}
		else
		{
			// no longer present or already changed or position change not properly accounted for
			_ASSERTE(!"bad offset during apply changes in SubstituteTemplateInstanceDefText");
			continue;
		}

		// fix up changes due to the change just made (only if replacement text len != orig text len)
		const int textDiff = cur.mReplacementText.GetLength() - cur.mTextMatch.GetLength();
		if (!textDiff)
			continue;

		for (auto& curFixup : changes)
		{
			// if item has position after cur, update pos due to text length change
			if (curFixup.mMatchPos > cur.mMatchPos)
				curFixup.mMatchPos += textDiff;
		}
	}

	return didReplace;
}

void SubstituteTemplateInstanceDefText(const WTString& constTemplateFormalDeclarationArguments,
                                       const WTString& constInstanceArguments, token2& def, bool& toTypeIsPtr,
                                       BOOL& didReplace)
{
	std::vector<WTString> templateFormalDeclarationArgumentList;
	SplitTemplateArgs(constTemplateFormalDeclarationArguments, templateFormalDeclarationArgumentList);

	std::vector<WTString> instanceArgumentList;
	SplitInstanceArgs(constInstanceArguments, instanceArgumentList);

	WTString defStr(def.Str());
	std::vector<ChangeItem> changes;

	// [case: 90371]
	// make substitutions in two passes.
	// first pass makes list of proposed changes.
	// second pass commits the changes.
	// Two passes are necessary to prevent changing a substitution that was made
	// for a previous argument (2 args; first changes T1 to int, second changes int to bool;
	// second change should not modify the int that was substituted for T1).

	// build list of proposed changes iterating over both
	// templateFormalDeclarationArgumentList and instanceArgumentList
	unsigned int instanceArgIdx = 0;
	for (auto curFormalArg : templateFormalDeclarationArgumentList)
	{
		if (curFormalArg.IsEmpty())
		{
			_ASSERTE(!"empty string in arg list");
			break;
		}

		WTString instanceArg;
		if (instanceArgIdx < instanceArgumentList.size())
			instanceArg = instanceArgumentList[instanceArgIdx++];

		{
			const int formalArgDefaultPos = curFormalArg.Find('=');
			if (instanceArg.IsEmpty())
			{
				if (0 == formalArgDefaultPos)
				{
					// [case: 94492]
					// formal arg: class = enable_if<...>
					// instance and formal args have no names, so no substitution needs to be performed
					break;
				}

				// no arg specified in instance, so check for formalDecl default value
				if (-1 != formalArgDefaultPos)
					instanceArg = curFormalArg.Mid(formalArgDefaultPos + 1);
			}

			if (-1 != formalArgDefaultPos)
			{
				if (!formalArgDefaultPos)
				{
					// [case: 94492]
					curFormalArg.Empty();
					vLog("ERROR: bad template arg substitute 1 formal( %s ) instance( %s )",
					     constTemplateFormalDeclarationArguments.c_str(), constInstanceArguments.c_str());
					break;
				}

				curFormalArg = curFormalArg.Left(formalArgDefaultPos);
				curFormalArg.Trim();
			}
		}

		{
			int pos = curFormalArg.ReverseFind(" ");
			if (-1 != pos)
				curFormalArg = curFormalArg.Mid(pos + 1);
		}

		instanceArg = DecodeScope(instanceArg);
		instanceArg.Trim();
		if (instanceArg.IsEmpty())
			continue;
		if (instanceArg.FindOneOf("*^") != -1 && defStr.Find(curFormalArg.c_str()) == 0)
			toTypeIsPtr = true;
		instanceArg = EncodeScope(instanceArg);

		if (curFormalArg.IsEmpty())
		{
			// [case: 94492]
			// last chance to prevent infinite while loop
			_ASSERTE(!"empty string in arg list (2)");
			vLog("ERROR: bad template arg substitute 2 formal( %s ) instance( %s )",
			     constTemplateFormalDeclarationArguments.c_str(), constInstanceArguments.c_str());
			break;
		}

		// find whole word positions
		const LPCSTR buf = defStr.c_str();
		LPCSTR fp = strstrWholeWord(buf, curFormalArg);
		while (fp)
		{
			int offset = ptr_sub__int(fp, buf);
			changes.push_back({curFormalArg, offset, instanceArg});

			fp += curFormalArg.GetLength();
			fp = strstrWholeWord(fp, curFormalArg);
		}
	}

	didReplace = ApplyTextSubstitutions(changes, defStr);

	if (templateFormalDeclarationArgumentList.size() && instanceArgumentList.size() && -1 != defStr.Find("typename"))
	{
		// #parseBadTemplateTypenameSubst
		// this hack is leftover from the original implementation.  The original
		// comment was:
		// fix for COM_SMARTPTR_TYPEDEF _IIID stuff
		// not clear why it is necessary.  something in va isn't handling
		// "typedef typename ..." properly.
		// It happens to fix COM_SMARTPTRs but probably doesn't fix anything else.
		// It's a hack that assumes the first instanceArgument is the type that
		// matters.
		// Leaving as is for an exercise some other time.
		defStr.ReplaceAll("typename", EncodeScope(instanceArgumentList[0]).c_str(), TRUE);
	}

	def = defStr;
}

using StringMap = std::map<WTString, WTString>;

void MapArgsToDefaultValues(const std::vector<WTString>& functionTemplateFormalDeclarationArgumentList,
                            StringMap& defaultArgMap)
{
	for (const auto& cur : functionTemplateFormalDeclarationArgumentList)
	{
		int pos = cur.Find('=');
		if (-1 == pos)
			return;

		WTString key(cur.Left(pos));
		key.Trim();

		WTString val(cur.Mid(pos + 1));
		val.Trim();

		if (key.IsEmpty() || val.IsEmpty())
			return;

		defaultArgMap[key] = val;
	}
}

void SubstituteFunctionTemplateInstanceDefaults(const WTString& functionTemplateFormalDeclarationArguments,
                                                const WTString& classTemplateFormalDeclarationArguments, token2& def)
{
	// [case: 112697]
	// separate out templateFormalDeclarationArguments2 into map mapping arg to the default value
	// iterate over list: if item has default value and it matches an arg in templateFormalDeclarationArguments list,
	// then do subst where subst is from arg to default value in def
	std::vector<WTString> functionTemplateFormalDeclarationArgumentList;
	SplitTemplateArgs(functionTemplateFormalDeclarationArguments, functionTemplateFormalDeclarationArgumentList);
	if (functionTemplateFormalDeclarationArgumentList.empty())
		return;

	StringMap defaultArgMap; // in <key = val>, key is name, val is default value
	MapArgsToDefaultValues(functionTemplateFormalDeclarationArgumentList, defaultArgMap);
	if (defaultArgMap.empty())
		return;

	std::vector<WTString> templateFormalDeclarationArgumentList;
	SplitTemplateArgs(classTemplateFormalDeclarationArguments, templateFormalDeclarationArgumentList);
	if (templateFormalDeclarationArgumentList.empty())
		return;

	StringMap substMap; // key is name, val is default value that is a template class arg name
	// (substMap will become a subset of defaultArgMap)
	for (const auto& curArg : defaultArgMap)
	{
		const WTString& defVal(curArg.second);
		for (const auto& classArg : templateFormalDeclarationArgumentList)
		{
			if (defVal == classArg)
			{
				substMap[curArg.first] = defVal;
				break;
			}
		}
	}

	if (substMap.empty())
		return;

	WTString defStr(def.Str());
	std::vector<ChangeItem> changes;

	// [case: 90371]
	// make substitutions in two passes.
	// first pass makes list of proposed changes.
	// second pass commits the changes.
	// Two passes are necessary to prevent changing a substitution that was made
	// for a previous argument (2 args; first changes T1 to int, second changes int to bool;
	// second change should not modify the int that was substituted for T1).

	// build list of proposed changes iterating over substMap
	for (const auto& mapItem : substMap)
	{
		const WTString& curFormalArg(mapItem.first);
		WTString instanceArg(DecodeScope(mapItem.second));
		instanceArg.Trim();
		if (instanceArg.IsEmpty())
			continue;
		instanceArg = EncodeScope(instanceArg);

		if (curFormalArg.IsEmpty())
		{
			// [case: 94492]
			// last chance to prevent infinite while loop
			_ASSERTE(!"empty string in arg list (2)");
			vLog("ERROR: bad func template arg substitute 2 formal( %s )",
			     classTemplateFormalDeclarationArguments.c_str());
			break;
		}

		// find whole word positions
		const LPCSTR buf = defStr.c_str();
		LPCSTR fp = strstrWholeWord(buf, curFormalArg);
		while (fp)
		{
			int offset = ptr_sub__int(fp, buf);
			changes.push_back({curFormalArg, offset, instanceArg});

			fp += curFormalArg.GetLength();
			fp = strstrWholeWord(fp, curFormalArg);
		}
	}

	ApplyTextSubstitutions(changes, defStr);

	def = defStr;
}

HTREEITEM
MyInsertItem(HWND m_hWnd, UINT nMask, const WTString& lpszItem, int nImage, int nSelectedImage, UINT nState,
             UINT nStateMask, const DType* pClsData, HTREEITEM hParent, HTREEITEM hInsertAfter)
{
	if (pClsData)
	{
		if (IsCFile(gTypingDevLang) && !pClsData->FileId())
		{
			// if there's no file, then it's template related and a dupe
			return NULL;
		}

		if (pClsData->IsMarkedForDetach())
			return NULL;
	}

	if (lpszItem.IsEmpty())
		return NULL;

	CStringW txt(lpszItem.Wide());
	TVINSERTSTRUCTW tvis;
	tvis.hParent = hParent;
	tvis.hInsertAfter = hInsertAfter;
	tvis.item.mask = nMask;
	tvis.item.pszText = (LPWSTR)(LPCWSTR)txt;
	tvis.item.iImage = nImage;
	tvis.item.iSelectedImage = nSelectedImage;
	tvis.item.state = nState;
	tvis.item.stateMask = nStateMask;
	tvis.item.lParam = (LPARAM)pClsData;

	HTREEITEM item = (HTREEITEM)::SendMessageW(m_hWnd, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
	if (lpszItem.Find("unnamed_") != -1)
	{
		// expand members of unnamed structs/unions since they are usually
		// available directly from the parent
		PostMessage(m_hWnd, TVM_EXPAND, (WPARAM)TVE_EXPAND, (LPARAM)item);
	}
	return item;
}

static DType* sNewCursor = NULL;
bool ShouldExcludeSymbolItem(DType* d1)
{
	_ASSERTE(sNewCursor);
	if (d1->SymHash() == sNewCursor->SymHash())
	{
		if (d1->FileId() == sNewCursor->FileId())
		{
			if (d1->HasEquivalentSymData(*sNewCursor))
				return true;
			if (d1->SymScope() == sNewCursor->SymScope())
			{
				if (d1->Line() == sNewCursor->Line()) // {...} diff in d1->mSymData->def
					return true;

				if ((d1->IsImpl() && (sNewCursor->Line() + 1) == d1->Line()) ||
				    (sNewCursor->IsImpl() && (d1->Line() + 1) == sNewCursor->Line()))
				{
					// check for inline implementation on line after declaration
					return AreSymbolDefsEquivalent(*d1, *sNewCursor);
				}
			}
		}
		else if (d1->SymScope() == sNewCursor->SymScope())
		{
			if (IsCFile(gTypingDevLang))
			{
				// file and line are different - could be declaration vs definition
				if ((d1->IsImpl() && !sNewCursor->IsImpl()) || (!d1->IsImpl() && sNewCursor->IsImpl()))
				{
					return AreSymbolDefsEquivalent(*d1, *sNewCursor);
				}
			}
			else if (gTypingDevLang == CS)
			{
				// system CS items come through multiple times but from different files
				if (d1->Line() == sNewCursor->Line() && d1->ScopeHash() == sNewCursor->ScopeHash() &&
				    d1->SymHash() == sNewCursor->SymHash() && d1->MaskedType() == sNewCursor->MaskedType() &&
				    d1->Attributes() == sNewCursor->Attributes() && d1->DbFlags() == sNewCursor->Attributes() &&
				    d1->Def() == sNewCursor->Def())
					return true;
			}
		}
	}
	return false;
}

void FileDic::ClassBrowse(LPCSTR scope, HWND hTree, HTREEITEM hItem, BOOL refresh)
{
	DB_READ_LOCK;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		CTreeCtrl* tree = (CTreeCtrl*)CWnd::FromHandle(hTree);
		const uint scopeID = WTHashKey(scope);
		const uint sz = m_pHashTable->TableSize();
		WTString lsym;
		DType* scopeData = NULL;
		uint state = 0; // (this == GetSysDic())?0:TVIS_BOLD;  // don't bold local classes per darren -Jer
		std::vector<DType*> itemsAdded;
		for (uint r = 0; r < sz; r++)
		{
			for (DefObj* cursor = m_pHashTable->GetRowHead(r); cursor; cursor = cursor->next)
			{
				if (scopeID != cursor->ScopeHash())
					continue;

				if (cursor->IsHideFromUser())
					continue;

				const uint type = cursor->MaskedType();
				if (!((scopeID || IS_OBJECT_TYPE(type)) && type != COMMENT && type != GOTODEF))
					continue;

				DType* data = cursor;
				WTString def = data->Def();
				if (type == TYPE && !def.contains("namespace"))
					continue;

				if (tree->GetCount() > 10000)
					return;

				if (g_currentEdCnt && EdPeekMessage(g_currentEdCnt->GetSafeHwnd()))
				{
					// bail to allow further scrolling
					if (g_CVAClassView)
						g_CVAClassView->m_lastSym = "";
					return;
				}

				WTString dataSymScope(data->SymScope());
				LPCSTR sym = dataSymScope.c_str();
				int img = GetTypeImgIdx(cursor->MaskedType(), cursor->Attributes());

				if (type == FUNC)
				{
					if (cursor->IsImpl())
					{
						bool tryAdd = true;
						// if there is another DType without V_IMPLEMENTATION,
						// then don't add the current one (only need to check current table row)
						for (DefObj* dCursor = m_pHashTable->GetRowHead(r); dCursor; dCursor = dCursor->next)
						{
							if (!dCursor->IsImpl() && dCursor->ScopeHash() == data->ScopeHash() &&
							    dCursor->SymHash() == data->SymHash())
							{
								if (dCursor->SymScope() == dataSymScope)
									tryAdd = false;
								break;
							}
						}

						if (!tryAdd)
						{
							// [case: 118695]
							// for back-compat with pre-existing behavior, we want =default ctor implementations in va
							// view
							if (!cursor->IsConstructor())
								continue;
							else if (!cursor->Def().contains("default"))
								continue;
						}
					}

					sNewCursor = cursor;
					if (std::find_if(itemsAdded.begin(), itemsAdded.end(), ::ShouldExcludeSymbolItem) ==
					    itemsAdded.end())
					{
						itemsAdded.push_back(cursor);
						const int nextdef = def.Find('\f');
						if (nextdef > 0)
							def = def.Mid(0, nextdef);
						// discard return values in function names
						const WTString symName = StrGetSym(sym);
						if (symName.GetLength())
						{
							int symIdx = def.Find("operator");
							if (symIdx < 0)
								symIdx = def.Find(symName.c_str());
							if (symIdx > 0)
								def = def.Mid(symIdx);
						}
						def.ReplaceAll('\t', ' ');
						int i = def.Find("{...}");
						if (i > 0)
							def = def.Mid(0, i);
						MyInsertItem(tree->GetSafeHwnd(),
						             TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE,
						             DecodeScope(def), img, img, state, state, cursor, hItem, TVI_LAST);
					}
					sNewCursor = NULL;
				}
				else
				{
					def.MakeLower();
					if (def == " " || (def.contains("typedef") && (!def.contains("class") && !def.contains("struct") &&
					                                               !def.contains("interface"))))
					{
						// don't add typdef's not typedef'd to classes
					}
					else if (lsym != sym && (type != TYPE || scopeID) && sym[0] != '<')
					{
						lsym = sym;
						lsym.ReplaceAll('\t', ' ');

						if (type == CLASS || type == STRUCT || type == TYPE || type == C_INTERFACE || type == C_ENUM ||
						    type == MODULE || type == NAMESPACE)
						{
							if (strcmp(sym, "enum") != 0 && strcmp(sym, "Base") != 0)
							{
								HTREEITEM i =
								    MyInsertItem(tree->GetSafeHwnd(),
								                 TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE,
								                 DecodeScope(StrGetSym(lsym)), img, img, state, state, cursor, hItem,
								                 refresh ? TVI_SORT : TVI_LAST);
								if (i)
								{
									MyInsertItem(tree->GetSafeHwnd(),
									             TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE,
									             uptrtos((uintptr_t)cursor), img, img, state, state, NULL, i, TVI_LAST);
									tree->InsertItem("...", 1, 1, i);
								}
							}
						}
						else if (_tcsicmp(lsym.c_str(), "base") != 0)
						{
							LPCTSTR theSym = StrGetSym(lsym);
							if (kEmptyTemplate == theSym)
								;
							else if (theSym)
							{
								if (!scopeData)
								{
									const WTString theScope(scope);
									FindData fds(&theScope, &theScope);
									scopeData = Find(&fds);
								}

								switch (type)
								{
								case C_ENUMITEM:
									// enum items defined in classes/structs have two scopes
									// one for the class and one for the enum group
									// don't display the one for the class -
									// restrict display to enum group [case: 795]
									if (scopeData && scopeData->type() != C_ENUM)
										continue;
									break;
								case FUNC:
								case VAR:
								case C_ENUM:
								case STRUCT:
									if (scopeData && (scopeData->type() == STRUCT || scopeData->type() == CLASS ||
									                  scopeData->type() == NAMESPACE))
									{
										// members of unnamed structs/unions have two scopes
										// one for the parent of the struct/union and one for the
										// unnamed struct/union - don't display the one
										// for the parent - restrict to the unnamed struct/union
										// [case: 3344,904]
										WTString structMemberReverseLookup(::GetUnnamedParentScope(lsym, "struct"));
										if (structMemberReverseLookup.IsEmpty())
											structMemberReverseLookup = ::GetUnnamedParentScope(lsym, "union");
										if (!structMemberReverseLookup.IsEmpty())
											continue;
									}
									break;
								}

								sNewCursor = cursor;
								if (std::find_if(itemsAdded.begin(), itemsAdded.end(), ::ShouldExcludeSymbolItem) ==
								    itemsAdded.end())
								{
									itemsAdded.push_back(cursor);
									MyInsertItem(tree->GetSafeHwnd(),
									             TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE,
									             theSym, img, img, state, state, cursor, hItem,
									             refresh ? TVI_SORT : TVI_LAST);
								}
								sNewCursor = NULL;
							}
						}
					}
				}
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FDC:CB:");
		Log("ERROR: exception caught in FD::CB");
	}
#endif // !SEAN
}

DType* FileDic::FindImplementation(DType& data)
{
	if (data.IsImpl())
		return &data;

	DB_READ_LOCK;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		for (DefObj* cursor = m_pHashTable->FindRowHead(data.SymHash()); cursor; cursor = cursor->next)
		{
			if (data.ScopeHash() != cursor->ScopeHash())
				continue;

			DType* curDat = cursor;

			const uint type = cursor->MaskedType();
			if ((type == FUNC && cursor->IsImpl()) || type == GOTODEF)
			{
				if (curDat->SymHash() == data.SymHash() &&
				    (curDat->FileId() != data.FileId() ||
				     (curDat->FileId() == data.FileId() && curDat->Line() != data.Line())))
				{
					if (curDat->SymScope() == data.SymScope() && AreSymbolDefsEquivalent(*curDat, data))
					{
						return curDat;
					}
				}
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FDD:");
		Log("ERROR: exception caught in FD::FindDef");
	}
#endif // !SEAN
	return NULL;
}

DType* FileDic::FindDeclaration(DType& data)
{
	DB_READ_LOCK;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		for (DefObj* cursor = m_pHashTable->FindRowHead(data.SymHash()); cursor; cursor = cursor->next)
		{
			if (data.ScopeHash() != cursor->ScopeHash())
				continue;

			DType* curDat = cursor;
			if (curDat == &data)
				continue;

			if (curDat->SymHash() == data.SymHash() &&
			    (curDat->FileId() != data.FileId() ||
			     (curDat->FileId() == data.FileId() && curDat->Line() < data.Line())))
			{
				if (curDat->SymScope() == data.SymScope() && AreSymbolDefsEquivalent(*curDat, data))
				{
					return curDat;
				}
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FDBFD:");
		Log("ERROR: exception caught in FDB::FindDec");
	}
#endif // !SEAN
	return NULL;
}

#ifdef _DEBUG
static thread_local bool failed_due_to_symMatchSkippedByType = false;
#endif
template<bool fast>
DType* FileDic::FindExactImpl(const WTString& sym, uint scopeID, BOOL concatDefs /*= TRUE*/, uint searchForType /*= 0*/,
                          bool honorHideAttr /*= true*/)
{
	int symMatchSkippedByType = 0;
	DEFTIMER(FindExactTimer);
	DefObj* first = nullptr;
	const uint hv = DType::HashSym(sym);
	const uint symHash2 = DType::GetSymHash2(sym.c_str());
	int concatCount = 0;
	const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();

	DB_READ_LOCK;
	const int doDBCase = fast ? true : g_doDBCase;
	UseHashEqualAC_local_fast;
	WTString cdDef;
	DefObj *cursor;
	if(fast)
		cursor = m_pHashTable->FindScopeSymbolHead(scopeID, hv);
	else
		cursor = m_pHashTable->FindRowHead(hv);
	// find match in list
	for (DefObj *next; cursor; cursor = next)
	{
		if(fast)
			next = VAHashTable::ScopeSymbolNodeToDefObj(cursor->node_scope_sym_index.next);
		else
			next = cursor->next;
		PreFetchCacheLine(PF_NON_TEMPORAL_LEVEL_ALL, next);

		if constexpr(!fast)
		{
			if (!(HashEqualAC_local_fast(cursor->SymHash(), hv)))
				continue;
		}

		const uint curType = cursor->MaskedType();
		if (searchForType && searchForType != curType)
		{
			// [case: 71747] scope has to check for macros very often
			// this is an optimization specifically for VaMacroDef*Arg.
			if (VaMacroDefArg == searchForType || searchForType == VaMacroDefNoArg)
			{
				// VaMacroDef*Arg should always be passed in with a scopeID.
				// otherwise some assumptions may have to be revisited.
				_ASSERTE(scopeID);

				if (cursor->SymMatch(symHash2))
				{
					// symMatchSkippedByType is count of hits of type
					// !VaMacroDef*Arg (when looking for VaMacroDef*Arg) with same
					// name as sym we are looking for.
					if (++symMatchSkippedByType > 25)
					{
						#ifdef _DEBUG
						failed_due_to_symMatchSkippedByType = true;
						#endif
						// more than 25 hits with same name that aren't
						// VaMacroDef*Arg.  Assume no remaining ones are either.
						return first;
					}
				}
			}
			continue;
		}

		if(!fast)
		{
			if (!(HashEqualAC_local_fast(cursor->ScopeHash(), scopeID)))
				continue;
		}

		if (honorHideAttr && cursor->IsHideFromUser())
		{
			switch (searchForType)
			{
			case CachedBaseclassList:
			case VaMacroDefArg:
			case VaMacroDefNoArg:
			case TEMPLATE_DEDUCTION_GUIDE:
			case TEMPLATETYPE:
				// override honorHideAttr if explicitly looking for a hidden type
				break;
			default:
				continue;
			}
		}

		if (GOTODEF == curType && GOTODEF != searchForType)
		{
			// Ignore GotoDef entries
			continue;
		}

		DType* cd = cursor;
		if (doDBCase)
		{
			// [case: 70618]
			/*if (CachedBaseclassList == searchForType)
			{
			    // CachedBaseclassList compares sym to SymScope() in whole
			    if (!cd->SymScope().EndsWith(sym))
			        continue;
			}
			else*/
			if (TEMPLATETYPE == searchForType)
			{
				// TEMPLATETYPE sometimes has empty string??
				// must do string compare rather than SymMatch to match previous behavior (pre-secondary hash)
				if (cd->Sym() != sym)
					continue;
			}
			else
			{
				// all other searches are for exact sym
				if (!cd->SymMatch(symHash2))
					continue; // collision on primary hash; see FreeFunction_test::testHashing
			}
		}
		else
		{
			// case-insensitive languages like javascript
			if (!cd->SymScope().EndsWithNC(sym))
				continue;
		}

		if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
			continue;

		// Concatenate the definitions like FDic::Find(fds);
		if (!first)
		{
			first = cursor;
			if (!concatDefs)
				break;
		}
		else if (curType != GOTODEF && vaHashtag != curType) // [case: 105593]
		{
			if (first->MaskedType() == GOTODEF)
				first = cursor;

			if (cursor->IsVaStdAfx())
			{
				// [case: 141427]
				// vastdafx items have priority over others -- change first to curent item
				const WTString firstDef(first->Def());
				if (!firstDef.IsEmpty())
				{
					const WTString curDef(cursor->Def());
					if (!ContainsField(curDef, firstDef))
					{
						// VaStdAfx def should always be first, but append def of the previous item
						const WTString newDef = curDef + kFormfeed + firstDef;
						cursor->SetDef(newDef);
					}
				}

				first = cursor;
				continue;
			}

			if (concatCount < 10)
			{
				AutoLockCs l(sConcatLock);
				// in case there is a class and a method with the same name, set the type to class so GetBaseClassList
				// still will work
				if (!first->IsType() && cursor->IsType())
					first->copyType(cursor);

				auto [cdDef_sv, cdDef_lock] = cd->Def_sv();
				if (cdDef_sv.length() < kMaxDefConcatLen)
				{
					if (curType != TEMPLATETYPE || sym != kEmptyTemplate)
					{
						cdDef = cdDef_sv;
						cdDef_lock.release();

						if (cdDef.Find('\f') != -1)
						{
							// Only the first should have multiple defs.
							cdDef.AssignCopy(TokenGetField2(cdDef, kFormfeed_ch));
							cd->SetDef(cdDef);
						}

						auto [firstDef_sv, firstDef_lock] = first->Def_sv();
						if (firstDef_sv.length() < kMaxDefConcatLen)
						{
							if (!ContainsField(firstDef_sv.data(), cdDef))
							{
								if (!cdDef.contains(kFormfeed.c_str()))
								{
									WTString newDef;
									if (first->IsVaStdAfx())
									{
										// [case: 141427]
										// VaStdAfx def should always be first
										newDef = {firstDef_sv, kFormfeed, cdDef};
									}
									else if (cd->IsPreferredDef())
										newDef = {cdDef, kFormfeed, firstDef_sv};
									else
										newDef = {firstDef_sv, kFormfeed, cdDef};
									firstDef_lock.release();

									first->SetDef(newDef);
									concatCount++; //   just in case there are hundred of definitions for foo
									if (concatCount >= 7 && g_loggingEnabled)
									{
										vLog("FindE: concatcount %s", first->SymScope().c_str());
										_ASSERT("FindE: concatCount exceeds 7");
									}
								}
							}
						}
						else
						{
							// [case: 104859] [case: 20327] sanity check
							vLog("WARN: FindE: concat def C len (%d) cnt(%d) (%s)", (int)firstDef_sv.length(), concatCount,
							     sym.c_str());
						}
					}
				}
				else
				{
					// [case: 20327] sanity check - prevent hang caused by growing top->Def() to over 120KB
					vLog("WARN: FindE: concat def B len (%d) cnt(%d) (%s)", (int)cdDef_sv.length(), concatCount,
					     sym.c_str());
				}
			}
		}
	}

	// return first definition
	return first;
}
DType* FileDic::FindExact(const WTString& sym, uint scopeID, BOOL concatDefs /*= TRUE*/, uint searchForType /*= 0*/,
							 bool honorHideAttr /*= true*/)
{
	// During large 'TOptional' findref in UE5:
	//   - FindExact is called 22.8mil times
	//   - rows are iterated 6.6bil times:
	//     - 7% is skipped by symbol hash check
	//     - 2% is skipped by searchForType check
	//     - 90% is skipped by scope hash check
	//   - 100% calls to FindExact has g_doDBCase == true
	//   - 68% calls to FindExact has searchForType
	// Using scope+symbol hashes index remove most iterations will be removed. The only difference is searchForType 
	// check is now done after the scope hash check. In the case of 'continue', it will be the same. 'return' case will 
	// be possibly different, but I believe more correct (it's some speed optimization to stop iterating if scope check
	// failed too many times).

	if (GetHashTable()->HasScopeSymbolIndex() && g_doDBCase)
	{
		auto ret = FindExactImpl<true>(sym, scopeID, concatDefs, searchForType, honorHideAttr);
/* #ifdef _DEBUG
		failed_due_to_symMatchSkippedByType = false;
		auto old = FindExactImpl<false>(sym, scopeID, concatDefs, searchForType, honorHideAttr);

// 		auto ret_sym = ret ? ret->Sym() : nullptr;
// 		auto ret_scope = ret ? ret->Scope() : nullptr;
// 		auto ret_def = ret ? ret->Def() : nullptr;
// 		auto old_sym = old ? old->Sym() : nullptr;
// 		auto old_scope = old ? old->Scope() : nullptr;
// 		auto old_def = old ? old->Def() : nullptr;
// 		if (ret != old)
// 			__debugbreak();

		auto are_same = [&]() {
			if((ret == old) || (ret && !old && failed_due_to_symMatchSkippedByType))
				return true;
			if (ret && old && (ret->Sym() == old->Sym()) && (ret->Def() == old->Def()) && (ret->Scope() == old->Scope()))
				return true;
			return false;
		};
		if (!are_same())
		{
			// retry if someone added a symbol between 'ret =' and 'old ='
			ret = FindExactImpl<true>(sym, scopeID, concatDefs, searchForType, honorHideAttr);
			assert(are_same());
		}
#endif*/
		return ret;
	}
	else
		return FindExactImpl<false>(sym, scopeID, concatDefs, searchForType, honorHideAttr);
}

    // See if sym is defined anywhere, for ASC
DType* FileDic::FindAnySym(const WTString& sym)
{
	DB_READ_LOCK;
	// find match in list
	DefObj* guess = NULL;
	const uint tSymHash = DType::GetSymHash(sym);
	for (DefObj* cursor = m_pHashTable->FindRowHead(tSymHash); cursor; cursor = cursor->next)
	{
		if (tSymHash != cursor->SymHash())
			continue;

#if defined(RAD_STUDIO)
		// [case: 150126] prevent from mixing VCL and FMX
		if (gVaRadStudioPlugin && gVaRadStudioPlugin->IsUsingFramework())
		{
			auto scope = cursor->Scope();
			if (!scope.IsEmpty())
			{
				if (gVaRadStudioPlugin->IsFmxFramework())
				{
					if (scope.FindNoCase(":Vcl:") >= 0)
						continue;
				}
				else if (gVaRadStudioPlugin->IsVclFramework())
				{
					if (scope.FindNoCase(":Fmx:") >= 0)
						continue;
				}
			}
		}
#endif

		// [case: 141700] don't call IsFromInvalidSystemFile
		// allow V_HIDEFROMUSER for forward declarations
		const uint curType = cursor->MaskedType();
		if (IS_OBJECT_TYPE(curType))
		{
			guess = cursor;
			if (!cursor->IsVaForwardDeclare())
				break;
		}

		if (curType == VaMacroDefArg || curType == VaMacroDefNoArg || curType == GOTODEF || curType == COMMENT ||
		    curType == CachedBaseclassList || curType == TEMPLATE_DEDUCTION_GUIDE || cursor->IsConstructor())
			continue;
		// DB is already case correct, this broke ":if" since definition did not contain "if"
		//		if(gTypingDevLang == CS){// make sure case matches
		//			DType * dat = cursor->GetData();
		//			if(dat && dat->Value().Find(sym)== -1)
		//				continue;
		//		}

		if (!m_isSysDic && cursor->IsSysLib())
		{
			// fixes coloring issues, template are members are stored to Global dict.
		}
		else if (!guess)
			guess = cursor;
	}

	return guess;
}

template<bool fast>
DType* FileDic::FindExactObjImpl(const WTString& sym, uint scopeID, bool honorHideAttr /*= true*/)
{
	DefObj* last = NULL;
	const uint hv = DType::HashSym(sym);
	const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();

	DB_READ_LOCK;

	DefObj *cursor;
	if constexpr (fast)
		cursor = m_pHashTable->FindScopeSymbolHead(scopeID, hv);
	else
		cursor = m_pHashTable->FindRowHead(hv);

	// find match in list
	for (; cursor; cursor = fast ? VAHashTable::ScopeSymbolNodeToDefObj(cursor->node_scope_sym_index.next) : cursor->next)
	{
		if constexpr (!fast)
		{
			if (!(cursor->SymHash() == hv && cursor->ScopeHash() == scopeID))
				continue;
		}

		if (honorHideAttr && cursor->IsHideFromUser())
			continue;

		DType* cd = cursor;
		if (cd && cd->SymScope().EndsWithNC(sym))
		{
			if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
				continue;

			last = cursor;
		}
	}

	return last;
}
DType* FileDic::FindExactObj(const WTString& sym, uint scopeID, bool honorHideAttr)
{
	if(m_pHashTable->HasScopeSymbolIndex())
	{
		DType* ret = FindExactObjImpl<true>(sym, scopeID, honorHideAttr);
		#ifdef _DEBUG
		DType* ret_orig = FindExactObjImpl<false>(sym, scopeID, honorHideAttr);
		assert(ret == ret_orig);
		#endif
		return ret;
	}
	else
		return FindExactObjImpl<false>(sym, scopeID, honorHideAttr);
}

DType* FileDic::GetFileData(const CStringW& fileAndPath) const
{
	DType* data = nullptr;
	const UINT fileId = gFileIdManager->GetFileId(fileAndPath);

	DB_READ_LOCK;
	for (DefObj* cursor = m_pHashTable->FindRowHead(fileId); cursor; cursor = cursor->next)
	{
		if (cursor->SymHash() != (int)fileId || cursor->ScopeHash())
			continue;

		if (cursor->FileId() != fileId)
			continue;

		data = cursor;
		if (data->IsDbLocal())
		{
			// save as second-best option; prefer IsDbSolution, IsDbCpp, etc
			continue;
		}

		break;
	}

	return data;
}

DType* GetUnnamedParent(const WTString& childItemScope, const WTString& parentType)
{
	DType* data = NULL;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		DB_READ_LOCK;
		const WTString symscope(":va_unnamed_" + parentType + "_scope" + childItemScope);
		const WTString sym = StrGetSym(symscope);
		const WTString scope = StrGetSymScope(symscope);
		data = GetSysDic()->FindExactObj(sym, WTHashKey(scope));
		if (!data)
			data = g_pGlobDic->FindExactObj(sym, WTHashKey(scope));
		if (!data)
		{
			EdCntPtr curEd = g_currentEdCnt;
			if (curEd)
				data = curEd->GetParseDb()->FindExact2(symscope.c_str());
		}
		if (!data)
		{
			MultiParsePtr mp = MultiParse::Create(gTypingDevLang);
			data = mp->FindExact2(symscope.c_str());
		}

		if (data)
			data->LoadStrs();
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("GEIP:");
		Log("ERROR: GEIP exception caught");
	}
#endif // !SEAN
	return data;
}

WTString GetUnnamedParentScope(const WTString& childItemScope, const WTString& parentType)
{
	WTString parentScope;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		DB_READ_LOCK;
		DType* parent = GetUnnamedParent(childItemScope, parentType);
		if (parent)
		{
			parentScope = parent->Def();
			int pos = parentScope.Find('\f');
			if (pos == -1)
				parentScope = StrGetSymScope(parentScope);
			else
			{
				const WTString tmp(parentScope.Left(pos));
				parentScope = StrGetSymScope(tmp);
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("GUPS:");
		Log("ERROR: GUPS exception caught");
	}
#endif // !SEAN
	return parentScope;
}

// reads definition from disk into memory
void BrowseSymbol(LPCSTR symScope, HWND hTree, HTREEITEM hItem)
{
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		DB_READ_LOCK;
		bool foundLocalData = false;
		WTString sym = StrGetSym(symScope);
		WTString scope = StrGetSymScope(symScope);
		DType* data = GetSysDic()->FindExactObj(sym, WTHashKey(scope));
		if (!data)
			data = g_pGlobDic->FindExactObj(sym, WTHashKey(scope));
		if (!data)
		{
			EdCntPtr curEd = g_currentEdCnt;
			if (curEd)
			{
				data = curEd->GetParseDb()->FindExact2(symScope);
				if (data && (curEd->m_ftype == Src || curEd->m_ftype == UC) && !data->HasLocalScope() &&
				    (data->IsMethod() || data->MaskedType() == DEFINE || data->MaskedType() == C_ENUM ||
				     data->MaskedType() == C_ENUMITEM || data->MaskedType() == CLASS || data->MaskedType() == STRUCT ||
				     data->MaskedType() == C_INTERFACE || data->MaskedType() == TYPE))
					foundLocalData = true;
			}
			if (!data)
			{
				MultiParsePtr mp = MultiParse::Create(gTypingDevLang);
				data = mp->FindExact2(symScope);
				if (!data)
					return;
			}
		}

		data->LoadStrs();
		const UINT orgAttrs(data->Attributes());

#define DATA_LEN 100000
		const uint len = min(foundLocalData ? 0u : data->IsMethod() ? 1u : 2u, (uint)sym.GetLength());
		WTString fileText = g_DBFiles.ReadDSFileText(data->FileId(), data->DbFlags());
		BOOL skipFirst = FALSE;

		if (fileText.GetLength() > DATA_LEN)
		{
			uint offset = data->GetDbOffset();
			// 			for (DWORD splitNum = 0; splitNum < data->GetDbSplit(); ++splitNum)
			// 			{
			// 				// what we want to do is get length of each split up to but not
			// 				// including data->GetDbSplit() but I don't see a way to get the
			// 				// size of a split.  Then the truncation below would work as
			// 				// intended and the assert could be removed.
			// 				offset += g_DBFiles.GetDSFileSplitLength(data->FileId(), data->DbFlags(), splitNum);
			// 			}

			if (offset > DATA_LEN)
			{
				// eat the first record since we're cutting up the file,
				// most likely leaving a partial record at the head
				skipFirst = TRUE;

				// this logic only fully works as intended for the first split
				// since offset is relative to start of split rather than start
				// of file.  If the sum of the lengths of the previous splits
				// is greater than the value of offset, no real damage done.
				// If the sum is less than the offset into data->GetDbSplit(),
				// then we might be loosing a chunk that we care about.
				fileText = fileText.Mid((size_t)offset - DATA_LEN);

				if (fileText.GetLength() > (DATA_LEN * 2))
				{
					// if this assert fires, then we are taking the chance that
					// the truncation that happens next completely dismisses the
					// part of the dsfile text that we care about.
					_ASSERTE(data->GetDbSplit() == 0);
					fileText = fileText.Mid(0, DATA_LEN * 2);
				}
			}
		}

		token t = fileText;
		CTreeCtrl* tree = (CTreeCtrl*)CWnd::FromHandle(hTree);
		if (skipFirst)
			(void)t.read("\n");
		WTString lastSym;
		while (t.more())
		{
			token ln = t.read("\n");
			const WTString vis = ln.read(DB_FIELD_DELIMITER_STR); // [case: 61110] [case: 58175] visibility flag
			_ASSERTE(vis == "0" || vis == "1");
			WTString s = ln.read(DB_FIELD_DELIMITER_STR);
			if (scope != StrGetSymScope(s))
				continue;

			WTString ssym = StrGetSym(s);
			if (ssym == lastSym)
				continue;

			if (len && strncmp(sym.c_str(), ssym.c_str(), len) != 0)
				continue;
			// scope match?

			WTString def = ln.read(DB_FIELD_DELIMITER_STR);
			WTString stype = ln.read(DB_FIELD_DELIMITER_STR);
			WTString sAttr = ln.read(DB_FIELD_DELIMITER_STR);

			uint type, attrs;
			type = attrs = 0;
			sscanf(stype.c_str(), "%x", &type);
			_ASSERTE((type & VA_DB_FLAGS_MASK) == (type & TYPEMASK));
			type &= TYPEMASK;
			sscanf(sAttr.c_str(), "%x", &attrs);
			if (!(attrs & V_HIDEFROMUSER) && (type != UNDEF || (type == UNDEF && !(attrs & V_SYSLIB))))
			{
				// don't filter based on pointer
				if ((attrs | V_POINTER) == (orgAttrs | V_POINTER) && type != RESWORD)
				{
					lastSym = ssym;
					const int img = GetTypeImgIdx(type, attrs);
					HTREEITEM i = MyInsertItem(tree->GetSafeHwnd(),
					                           TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE,
					                           DecodeScope(ssym), img, img, (ssym == sym) ? TVIS_SELECTED : 0u,
					                           TVIS_SELECTED, NULL, hItem, TVI_LAST);
					if (ssym == sym && i)
						tree->SelectItem(i);
				}
			}
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("BrsSym:");
		Log("ERROR: BrsSym exception caught");
	}
#endif // !SEAN
}

ULONG FileDic::GetCount()
{
	DB_READ_LOCK;
	return m_pHashTable->GetItemsInContainer();
}

WTString FileDic::GetArgLists(LPCSTR symScope)
{
	DB_READ_LOCK;
	WTString sym = StrGetSym(symScope);
	WTString scope = StrGetSymScope(symScope);
	DWORD symhash = WTHashKey(sym);
	DWORD scopehash = WTHashKey(scope);
	WTString argList;
	DEFTIMER(GetArgListsTmr);
	// find match in list
	for (DefObj* cursor = m_pHashTable->FindRowHead(symhash); cursor; cursor = cursor->next)
	{
		if (!((DWORD)cursor->ScopeHash() == scopehash && (DWORD)cursor->SymHash() == symhash))
			continue;

		DType* dat = cursor;
		WTString datDef(dat->Def());
		LPCSTR p = datDef.c_str();
		int p1;
		for (p1 = 0; p[p1] && p[p1] != '('; p1++)
			;
		if (p[p1])
			p1++;
		int p2;
		for (p2 = p1; p[p2] && p[p2] != ')'; p2++)
			;
		if (p[p2])
			argList += CompletionSetEntry(datDef.Mid(p1, p2 - p1).c_str());
	}
	return argList;
}

int FileDic::FindExactList(LPCSTR sym, uint scopeID, DTypeList& list, int max_items)
{
	UINT hv;
	if (sym[0] == DB_SEP_CHR)
		hv = ::WTHashKey(::StrGetSym(sym));
	else
		hv = ::WTHashKey(sym);
	return FindExactList(hv, scopeID, list, max_items);
}

int FileDic::FindExactList_orig(uint symHash, uint scopeID, DTypeList& list, int max_items)
{
	DB_READ_LOCK;
	int cnt = 0;
	const int doDBCase = g_doDBCase;
	UseHashEqualAC_local_fast;
	const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();

	// find match in list
	for (DefObj* cursor = m_pHashTable->FindRowHead(symHash); cursor && (cnt < max_items); cursor = cursor->next)
	{
		const uint curSymHash = cursor->SymHash();
		if (HashEqualAC_local_fast(curSymHash, symHash))
		{
			if (-1 == scopeID)
			{
				if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
					continue;

				list.emplace_back(cursor);
				cnt++;
			}
			else
			{
				const uint curScopeHash = cursor->ScopeHash();
				if (HashEqualAC_local_fast(curScopeHash, scopeID))
				{
					if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
						continue;

					list.emplace_back(cursor);
					cnt++;
				}
			}
		}
	}

	return cnt;
}
int FileDic::FindExactList_fast(uint symHash, uint scopeID, DTypeList& list, int max_items)
{
	DB_READ_LOCK;
	int cnt = 0;
	const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();

	// find match in list
	for (DefObj* cursor = m_pHashTable->FindScopeSymbolHead(scopeID, symHash); cursor && (cnt < max_items); cursor = VAHashTable::ScopeSymbolNodeToDefObj(cursor->node_scope_sym_index.next))
	{
		if (checkInvalidSys && ::IsFromInvalidSystemFile(cursor))
			continue;

		list.emplace_back(cursor);
		cnt++;
	}

	return cnt;
}
int FileDic::FindExactList(uint symHash, uint scopeID, DTypeList& list, int max_items)
{
	if(g_doDBCase && (scopeID != -1) && m_pHashTable->HasScopeSymbolIndex())
	{
// 		#ifdef _DEBUG
// 		DTypeList list_orig(list);
// 		int ret_orig = FindExactList_orig(symHash, scopeID, list_orig);
// 		#endif
		int ret = FindExactList_fast(symHash, scopeID, list, max_items
		);
//		assert((ret == ret_orig) && (list == list_orig));
		return ret;
	}
	else
		return FindExactList_orig(symHash, scopeID, list, max_items);
}

void FileDic::ForEach(DTypeForEachCallback fn, bool& stop)
{
	DB_READ_LOCK;
	const bool checkInvalidSys = s_pSysDic && s_pSysDic->GetHashTable() == GetHashTable();
	uint numRows = m_pHashTable->TableSize();
	auto forEach = [&](uint r) {
		for (DefObj* cursor = m_pHashTable->GetRowHead(r); cursor && !stop; cursor = cursor->next)
		{
			// [case: 65910]
			fn(cursor, checkInvalidSys);
		}
	};

#pragma warning(push)
#pragma warning(disable : 4127)
	if (true || Psettings->mUsePpl)
		Concurrency::parallel_for((uint)0, numRows, forEach);
	else
		::serial_for<uint>((uint)0, numRows, forEach);
#pragma warning(pop)
}

DType* FileDic::FindMatch(DTypePtr symDat)
{
	_ASSERTE(symDat);
	DB_READ_LOCK;

	// find match in list
	for (DefObj* cursor = m_pHashTable->FindRowHead(symDat->SymHash()); cursor; cursor = cursor->next)
	{
		if (*cursor == *symDat.get())
			return cursor;
	}

	return nullptr;
}

void FileDic::add(const DType* dtype)
{
	return add(DType(dtype));
}
void FileDic::add(DType&& dtype)
{
	_ASSERTE(dtype.MaskedType() || dtype.Attributes());
	if (dtype.IsDbBackedByDataFile() && !dtype.IsDbExternalOther())
		m_modified++;

	DB_READ_LOCK;
	m_pHashTable->CreateEntry(std::move(dtype));
}

void FileDic::SortIndexes()
{
	DatabaseDirectoryLock l2;
	::SetStatus(IDS_SORTING);
	CStringW dbDir(GetDBDir());
	_ASSERTE(!dbDir.IsEmpty());
	::SortLetterFiles(dbDir);
	dbDir = dbDir + kDbIdDir[DbTypeId_LocalsParseAll] + L"\\";
	::SortLetterFiles(dbDir);
}

void FileDic::LoadComplete()
{
	m_loaded = TRUE;
	m_modified = 0;
}

void FileDic::SetDBDir(const CStringW& dir)
{
	AutoLockCs l(mDbDirStringLock);
	m_dbDir = dir;
	m_loaded = false;
	vCatLog("Environment.Directories", "FD::SetDBDir %s", (LPCTSTR)CString(dir));
}
