#include "stdafxed.h"
#include "foo.h"
#include "expansion.h"
#include "project.h"
#include "ParseThrd.h"
#include "DBFile/VADBFile.h"
#include "FileTypes.h"
#include "file.h"
#include "wt_stdlib.h"
#include "Settings.h"
#include "Lock.h"
#include "FileId.h"
#include "assert_once.h"
#include "Usage.h"
#include "WrapCheck.h"
#include "TokenW.h"
#include "SymbolPositions.h"
#include "../common/ThreadStatic.h"
#include "../common/ThreadName.h"
#include "VAFileView.h"
#include "SpinCriticalSection.h"
#include <atomic>
#include "../common/ScopedIncrement.h"
#include "VAParse.h"
#include "SymbolRemover.h"
#include "../vaIPC/vaIPC/common/string_utils.h"

remote_heap::remote_heap_impl* rheap;
sdata DType::sdata_map::empty_sdata;
#if (defined(_DEBUG) || defined(VA_CPPUNIT)) && !defined(NO_REMOTE_HEAP)
bool remote_heap::fail_next_filemap = false;
#endif
#ifdef _DEBUG
thread_local std::unordered_set<std::atomic_int*> DType::deadlock_check;
#endif

void init_rheap()
{
	assert(!rheap);
	rheap = new remote_heap::remote_heap_impl;
}
void cleanup_rheap()
{
	delete rheap;
	rheap = nullptr;
}

size_t SymDefStrStruct::Emplace(void* buffer, const char* s_str, size_t s_len, const char* d_str, size_t d_len, size_t buffer_size)
{
	if (!s_str)
		s_str = "";
	if (s_len == size_t(-1))
		s_len = strlen(s_str);
	assert(strlen(s_str) == s_len);
	if (d_str && (d_len == size_t(-1)))
		d_len = strlen(d_str);
	assert(!d_str || (strlen(d_str) == d_len));

	size_t size = sizeof(SymDefStrStruct) + s_len + 1;
	if (d_str && d_len)
		size += d_len + 1;
	if (!buffer)
		return size;

	SymDefStrStruct* sdss = ::new (buffer) SymDefStrStruct;
	if (d_str && d_len)
	{
		strcpy_s(sdss->GetTrailingBytes(), size - sizeof(SymDefStrStruct), s_str);
		strcpy_s(&sdss->GetTrailingBytes()[s_len + 1], size - (sizeof(SymDefStrStruct) + s_len + 1), d_str);
		sdss->def_len = uint16_t(d_len + 1);
		assert(sdss->def_len == (d_len + 1)); // range check
		assert(strlen(s_str) == s_len);
		assert(strlen(d_str) == d_len);
	}
	else
	{
		strcpy_s(sdss->GetTrailingBytes(), size - sizeof(SymDefStrStruct), s_str);
		sdss->def_len = 0;
		assert(strlen(s_str) == s_len);
	}
	sdss->scope_len = uint16_t(s_len + 1);
	assert(sdss->scope_len == (s_len + 1)); // range check
	assert(size == buffer_size);

	sdss->InitSym(); // do it immediately
	return size;
}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(8) // the default
#endif

#ifdef _DEBUG
// try to make WTString inlines stay inline in debug builds
#pragma inline_depth(0) // the default
#endif

#if _MSC_VER > 1200
using std::filebuf;
using std::ifstream;
using std::ios;
using std::ofstream;
using std::streampos;
#endif
using OWL::string;

CSpinCriticalSection gSymDefStrsDeleteLock;
std::vector<SymDefStrStruct*> gSymDefStrsPendingDelete;
// Win32Heap gSharedVaHashTableHeap;

#ifdef COUNT_DTYPE_INSTANCES
LONG gEmptyDef = 0;
LONG gNotEmptyDef = 0;
LONG gEmptySymData = 0;
LONG gNotEmptySymData = 0;
#endif

SymbolVisibility DType::GetVisibility() const
{
	if (IsPublished())
		return vPublished;

	if (IsPrivate())
	{
		if (IsProtected())
			return vPrivateProtected;
		return vPrivate;
	}

	if (IsProtected())
	{
		if (IsInternal())
			return vProtectedInternal;
		return vProtected;
	}

	if (IsInternal())
		return vInternal;

	_ASSERTE(IsPublic());
	return vPublic;
}

CStringW DType::FilePath() const
{
	return gFileIdManager->GetFileForUser(FileId());
}

CStringW DType::GetSymbolLocation(int& lineOut) const
{
	if (!FileId())
	{
		lineOut = -1;
		return CStringW();
	}

	lineOut = mLine;
	return gFileIdManager->GetFileForUser(FileId());
}

FindData::FindData(const WTString* sym /* = NULL */, const WTString* scope /* = NULL */,
                   const WTString* baseclasslist /* = NULL */, const WTString* namespaces /*= NULL*/,
                   int findFlags /*= FDF_NONE*/)
    : mNamespaces(namespaces), findFlag(findFlags)
{
	_ASSERTE(!(findFlags & FDF_GotoDefIsOk && findFlags & FDF_TYPE) && "incompatible flags");
	this->sym = sym;
	this->scope = scope;
	this->baseclasslist = baseclasslist;
	scoperank = -9999;
	record = NULL;
	WTString bcl = baseclasslist ? *baseclasslist : NULLSTR;
	bcl.ReplaceAll(WILD_CARD_SCOPE, "");
#ifdef _DEBUG
	if ((uint)bcl.GetLength() >= _tcslen(WILD_CARD_SCOPE) && bcl.contains(WILD_CARD_SCOPE))
	{
		// don't expand every member of every class for T::
		token t = /*bcl*/ baseclasslist ? *baseclasslist : NULLSTR;
		t.ReplaceAll(WILD_CARD_SCOPE, "");
		WTString bcl2 = t.Str();
		assert(bcl == bcl2);
	}
#endif
	WTString ns = namespaces ? *namespaces : NULLSTR;
	scopeArray.Init(scope ? *scope : NULLSTR, bcl, ns, !!(findFlags & FDF_SplitBclForScope));
}

/////////////////////////////////////////////
// ScopeHashAry
// Holds an array of hash vals of each valid scope for a sym

void ScopeHashAry::Init(const WTString& scope, const WTString sbcl, const WTString namespaces,
                        bool splitBclForScope /*= false*/)
{
	mArry.clear();
	mArry_map.clear();
	mArry_map_hcm.clear();
#ifdef _WIN64
	mArry.reserve(MAX_LAYER);
	mArry_map.reserve(MAX_LAYER * 10);
	mArry_map_hcm.reserve(MAX_LAYER * 10);
#endif
#ifdef _DEBUG
	mArryStrs.clear();
	WTString tmpStr;
#endif

	mDeep = -1;
	UINT nHash = 0;
	LPCSTR bcl = sbcl.c_str();

	// Local Scope :foo:bar
	if (!scope.IsEmpty())
	{
		//  add global symbols to list
		AddItem(0,
#ifdef _DEBUG
		        "",
#endif
				true
		);
		mDeep = 0;
		for (int i = 0; scope[i]; i++)
		{
			if (GetCount() >= MAX_LAYER)
				return;
			char c = scope[i];
			if (c == '.')
				c = ':';

			if (scope[i] == ':' && scope[i + 1] == ':')
				i++; // catch :: base class list
			if (nHash && c == DB_SEP_CHR)
			{
				mDeep = GetCount();
				AddItem(nHash,
#ifdef _DEBUG
				        tmpStr,
#endif
						true
				);
			}

#ifdef _DEBUG
			tmpStr += c;
#endif // _DEBUG
			nHashAdd(c);

			if (!scope[i + 1])
			{
				mDeep = GetCount();
				AddItem(nHash,
#ifdef _DEBUG
				        tmpStr,
#endif
				        true);
			}
		}
	}

	// inherited scope CWnd:, CObject:, ...
	const bool canContinue = AddList(bcl, splitBclForScope);
	mUsingsStartIdx = GetCount();
	if (!canContinue)
	{
		// ran out of space
		return;
	}

	// using namespaces
	if (namespaces.IsEmpty())
		return;

	bcl = namespaces.c_str();
	AddList(bcl, false);
}

bool ScopeHashAry::AddList(LPCSTR bcl, bool addSubScopes)
{
	if (GetCount() >= MAX_LAYER)
		return false;

#ifdef _DEBUG
	using SubScopeItem = std::pair<uint, WTString>;
#else
	using SubScopeItem = uint;
#endif
	std::vector<SubScopeItem> subItems;

	uint nHash = 0;
	int i = 0;
#ifdef _DEBUG
	WTString tmpStr;
#endif
	for (; bcl && bcl[i]; i++)
	{
		if (bcl[i] == '\f' || !bcl[i + 1]) // :: separated str
		{
			if (bcl[i] != '\f' && bcl[i] != DB_SEP_CHR)
			{
				nHashAdd(bcl[i]); // in case it isn't terminated with \f
#ifdef _DEBUG
				tmpStr += bcl[i];
#endif
			}

			if (nHash)
			{
#ifdef _DEBUG
				if (!AddItem(nHash, tmpStr))
					return false;
#else
				if (!AddItem(nHash))
					return false;
#endif
				nHash = 0;
			}

#ifdef _DEBUG
			tmpStr.Empty();
#endif
		}
		else
		{
			if (bcl[i] == ':')
			{
				if (nHash && addSubScopes)
				{
					// [case: 109768]
					// add sub-scopes after all full scopes added, and then add them
					// in reverse order (:foo:bar:baz, :foo:bar, :foo), so
					// that longer scopes (more specific) get better rank (smaller index)
					// than shorter (more general) scopes.
#ifdef _DEBUG
					subItems.emplace_back(std::make_pair(nHash, tmpStr));
#else
					subItems.push_back(nHash);
#endif
				}

				if (bcl[i + 1] == ':')
				{
					// catch :: in base class list
					i++;
				}
			}

			nHashAdd(bcl[i]);
#ifdef _DEBUG
			tmpStr += bcl[i];
#endif
		}
	}

	if (addSubScopes)
	{
		for (const auto& it : subItems | std::views::reverse)
		{
#ifdef _DEBUG
			if (!AddItem(it.first, it.second))
				return false;
#else
			if (!AddItem(it))
				return false;
#endif
		}
	}

	return true;
}

// returns false if no space left
bool ScopeHashAry::AddItem(uint nHash, 
#ifdef _DEBUG
                           const WTString& tmpStr, 
#endif
                           bool force)
{
// 	if(!force && mArry_map.contains(nHash))
// 		return true;

	if (force || (nHash != (UINT)DB_SEP_CHR))
	{
		int i = (int)GetCount();
		auto [it, inserted] = mArry_map.insert({nHash, i});
		if(!inserted)
		{
			if(!force)
				return true;
			else
				it->second = i;
		}
//		mArry_map[nHash] = i;
		mArry_map_hcm[nHash & HASHCASEMASK] = i;
		mArry.push_back(nHash);
#ifdef _DEBUG
		mArryStrs[nHash] = tmpStr;
#endif
	}

	return GetCount() < (MAX_LAYER - 1);
}

// Search scope to see if symHash is local, inherited, global or found via using
// > 0		: Local var/in scope
// 0		: not found
// < 0		: inherited scope, member of inherited class (< 0 && > -1000)
// == -1000	: global symbol
// < -1000	: using namespace (< -1000 && > -2000) [case: 67128]
// == -2000	: global && looking for global && empty array
int ScopeHashAry::Contains(uint hsh) const
{
	if (!GetCount() && !hsh)
	{
		// isglobal and looking for global
		return -2000;
	}

	if (g_doDBCase)
	{
		auto it = mArry_map.find(hsh);
		if (it != mArry_map.end())
			return RfromI(it->second);
	}
	else
	{
		auto it = mArry_map_hcm.find(hsh & HASHCASEMASK);
		if (it != mArry_map_hcm.end())
			return RfromI(it->second);
	}

	// not found in scope
	return 0;
}
int ScopeHashAry::RfromI(int i) const
{
	if (i == 0 && mDeep >= 0)
		return -1000; // global
	if (i <= mDeep)
		return i + 1; // inscope
	else if (i < mUsingsStartIdx)
		return -(i + 1); // inherited
	else
		return -(i + 1 + 1000); // using namespace
}

BOOL DType::HasLocalScope()
{
	return IsLocalScope(SymScope());
}

BOOL DType::IsLocalScope(WTString scope)
{
	return scope.Find('-') != -1;
}

uint DType::HashSym(LPCSTR sym)
{
	uint hv = 0;
	if (sym)
	{
#ifdef jer
		ASSERT_ONCE(sym[0] != ':');
#endif
		// this is only for symbols - WTHashKey does not work for files
		hv = ::WTHashKey(sym);
	}
	return hv;
}

uint DType::GetSymHash2(LPCSTR sym)
{
	if (!sym || !sym[0])
		return 0;

	uint nHash = 0;
	while (*sym)
	{
		// this is similar in structure to nHashAdd, but uses a different
		// shift so that GetSymHash2 will result in different value than
		// WTHashKey when sym is all lower case.
		nHash = (nHash << 6) + nHash + *sym++;
	}
	return nHash;
}
uint DType::GetSymHash2_sv(std::string_view sv)
{
	if (sv.empty())
		return 0;

	uint nHash = 0;
	for (char ch : sv)
	{
		// this is similar in structure to nHashAdd, but uses a different
		// shift so that GetSymHash2 will result in different value than
		// WTHashKey when sym is all lower case.
		nHash = (nHash << 6) + nHash + ch;
	}
	return nHash;
}

void DType::ReloadStrs()
{
	// reads definition from disk into memory

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		if (FileId() && IsDbBackedByDataFile() && !IsMarkedForDetach())
			g_DBFiles.GetSymDefStrs(this, true); // this will call SetStrs
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("FOO:");
		Log("Error: Exception in DType::ReloadStrs");
		return;
	}
#endif
}

uint DType::GetSymHash(const WTString& sym)
{
	if (sym[0] != DB_SEP_CHR)
	{
		return HashSym(sym.c_str());
	}
	else
	{
		return WTHashKey(StrGetSym(sym.c_str()));
	}
}

uint DType::GetScopeHash(const WTString& sym)
{
	if (sym[0] != DB_SEP_CHR)
	{
		return 0;
	}
	else
	{
		return WTHashKey(StrGetSymScope(sym.c_str()).c_str());
	}
}

void DType::sdata_map::copy_from(const DType* from)
{
	assert(from);

	if (!dtype)
		return;

	LockForDtypeStringWrite ws(dtype);
	if (!from || !from->sdata_rptr)
	{
		delete_rptr();
		return;
	}

	remote_heap::stats::timing t(remote_heap::stats::stat::timing_sdatamap_copyfrom_us,
	                             remote_heap::stats::stat::timing_sdatamap_copyfrom_cnt);

	CheckoutForDtypeStringRead rs(from);
	sdata_map from_map(from);
	if (!from_map.get_ptr())
		return; // mapping failed
	size_t from_block_size = remote_heap::remote_heap_impl::get_block_size(from_map->get_size());
	size_t to_block_size = 0;

	if (dtype->sdata_rptr)
	{
		// try to reuse existing block
		to_block_size = remote_heap::remote_heap_impl::get_block_size(get_ptr()->get_size());
		if (!get_ptr() || (to_block_size < from_block_size))
		{
			delete_rptr();
			remote_heap::stats::increase_counter(remote_heap::stats::stat::sdatamap_block_not_reused);
		}
		else
			remote_heap::stats::increase_counter(remote_heap::stats::stat::sdatamap_block_reused);
	}
	if (!dtype->sdata_rptr)
	{
		dtype->sdata_rptr = rheap->alloc(from_map->get_size());
		to_block_size = remote_heap::remote_heap_impl::get_block_size(from_map->get_size());
	}

	assert(dtype->sdata_rptr);
	assert(get_ptr());
	if (dtype->sdata_rptr && get_ptr())
	{
#ifndef NO_REMOTE_HEAP
		(void)to_block_size;
		memcpy_s(get_ptr(), std::get<remote_heap::remote_heap_impl::area*>(dtype->sdata_rptr.deconstruct())->block_size,
		         from_map.get_ptr(), from_map->get_size());
#else
		memcpy_s(get_ptr(), to_block_size, from_map.get_ptr(), from_map->get_size());
#endif
	}
}

DType& DType::operator=(nullptr_t)
{
	_ASSERTE(0 == mReadWriteCount);

	if (sdata_rptr)
	{
		LockForDtypeStringWrite ws(this); // no symstrs_t should be alive at this point
		sdata_map(this).delete_rptr();
	}
	return *this;
}
DType::~DType()
{
	*this = nullptr;

	decrease_counter(remote_heap::stats::stat::dtypes);
	decrease_counter(remote_heap::stats::stat::dtypes_bytes, sizeof(*this));
}

void DType::SetStrs(const WTString& symscope, const WTString& def)
{
	LockForDtypeStringWrite ws(this);
	sdata_map(this).add_strings(symscope, def);
}

symstrs_t DType::GetStrs(bool getBoth /*= FALSE*/) const
{
	remote_heap::stats::timing t(remote_heap::stats::stat::timing_GetStrs_us,
	                             remote_heap::stats::stat::timing_GetStrs_cnt);

	increase_counter((remote_heap::stats::stat)(size_t)remote_heap::stats::reason::GetStrs);
	static const auto get_empty_symstrs = []() -> symstrs_t { return {{}, {}, nullptr, nullptr, 0}; };

	// used optional to be able lock/unlock; optional doesn't perform memory allocation
	std::optional<CheckoutForDtypeStringRead> rs{this};
	sdata_map me(this);
	auto* symdata = (sdata_rptr && me.get_ptr()) ? &me->mSymData : nullptr;
	if (sdata_rptr && !symdata)
		return get_empty_symstrs(); // mapping failed

	if (!symdata || !symdata->HasScope() || getBoth && !symdata->HasDef())
	{
#if !defined(SEAN)
		try
#endif
		{
			if (mFileID && IsDbBackedByDataFile() && !IsMarkedForDetach())
			{
				// release readerLock so that we can write
				rs.reset();

				g_DBFiles.GetSymDefStrs(const_cast<DType*>(this), // somehow get rid of const_cast?
				                        true /*getBoth*/,
				                        [&me, &symdata, this](const WTString& symscope, const WTString& def) {
					                        LockForDtypeStringWrite ws(this);
					                        if (me.add_strings(symscope, def))
						                        symdata = &me->mSymData;
				                        });

				// reset readerLock for reads that follow
				rs.emplace(this);
			}

			if (!me->mSymData.HasScope() || getBoth && !me->mSymData.HasDef())
			{
				// release read status so the write can occur
				rs.reset();
				{
					LockForDtypeStringWrite ws(this);
					me.delete_rptr();
					// delete_rptr will do the following from the original code:
					// if(!mSymData)
					//		AllocSymData(NULLSTR, NULLSTR);
				}
				// reclaim read status
				rs.emplace(this);

				return get_empty_symstrs();
			}
		}
#if !defined(SEAN)
		catch (...)
		{
			VALOGEXCEPTION("FOO:");
			Log("Error: Exception in DType::GetStrs");
		}
#endif
	}

	assert(rs);
	return {std::move(*rs), // used to ensure rptr is valid for the life of the created object
	        std::move(me), // symdata is good after move!
	        symdata && symdata->HasScope() ? symdata->PrivateGetSymScopeStr() : nullptr,
	        (getBoth && symdata && symdata->HasDef()) ? symdata->PrivateGetDefStr() : nullptr,
	        symdata ? symdata->SymOffset() : 0};
}

void DType::LoadStrs(bool force /*= false*/) const
{
	// In the old implementation, this function explicitly loads strings.
	// In the new implementation, this function is unnecessary as loading strings is moved to GetStrs().
	// However, if HasData() is going to be used, forced LoadStrs() should be used

	if (force && !sdata_rptr)
		(void)GetStrs(true);
}

bool DType::IsEquivalentIgnoringDbBacking(const DType& rhs) const
{
	// similar to DType::operator== except that DbSplit and DbOffset are ignored
	if (ScopeHash() != rhs.ScopeHash())
		return false;
	if (SymHash() != rhs.SymHash())
		return false;
	if (SymHash2() != rhs.SymHash2())
		return false;
	if (mTypeAndDbFlags != rhs.mTypeAndDbFlags)
		return false;
	if (Attributes() != rhs.Attributes())
		return false;
	if (FileId() != rhs.FileId())
		return false;
	// 	if (GetDbSplit() != rhs.GetDbSplit())
	// 		return false;
	// 	if (GetDbOffset() != rhs.GetDbOffset())
	// 		return false;
	if (Line() != rhs.Line())
		return false;

	return HasEquivalentSymData(rhs);
}

CStringW GetSymbolLocation(const DType& data)
{
	int line;
	const CStringW file(data.GetSymbolLocation(line));
	if (file.IsEmpty())
		return file;

	CStringW res;
	CString__FormatW(res, L"%s:%d", (LPCWSTR)file, line);
	return res;
}

bool GotoSymbol(DType& data)
{
	int line;
	const CStringW file(data.GetSymbolLocation(line));
	if (file.IsEmpty())
	{
#ifdef _DEBUGxx // also requires hasFileLine in EdCnt2 to be set to true
		if (gDte && data.IsDbNet())
		{
			// [case: 82619]
			WTString sym(data.SymScope());
			if (-1 != sym.Find("-"))
			{
				sym = data.Sym();
			}
			else
			{
				if (sym[0] == DB_SEP_CHR)
					sym = sym.Mid(1);
				sym.ReplaceAll(DB_SEP_CHR, '.');
			}

			// this is painfully slow and the window doesn't open until
			// after the search is complete
			gDte->ExecuteCommand(CComBSTR(L"View.ObjectBrowserSearch"), CComBSTR(sym));
			return true;
		}
#endif // _DEBUG

		return false;
	}

	::VAProjectAddScopeMRU(data.SymScope().c_str(), mruMethodNav);
	BOOL func = data.MaskedType() == GOTODEF;
	DelayFileOpen(file, line, data.Sym().c_str(), TRUE, func);
	return true;
}

CStringW GetSymbolLoc(LPCTSTR sym, int& defLineOut)
{
	WTString SymScope(sym);
	if (!SymScope.GetLength())
	{
		defLineOut = 0;
		return CStringW();
	}

	FileDic* sysDic = GetSysDic();

	// gotodef from main menu, goto if only one choice, or else pop up menu
	SymbolPosList posList;
	sysDic->FindDefList(SymScope, posList);
	g_pGlobDic->FindDefList(SymScope, posList);
	SymbolPosList::const_iterator it = posList.begin();
	if (it == posList.end())
	{
		defLineOut = 0;
		return CStringW();
	}

	SymbolPosList finalList;
	for (; it != posList.end(); ++it)
	{
		if (finalList.Contains((*it).mFileId, (*it).mLineNumber))
			continue;

		const CStringW curFile = gFileIdManager->GetFile((*it).mFilenameLower);
		if ((*it).mDisplayText.Find(L'{') != -1 || -1 != curFile.Find(L".rc"))
		{
			finalList.Add(*it);
			break;
		}

		if (-1 != curFile.Find(L".h"))
			finalList.AddHead(*it); // put headers at the top of list
		else
			finalList.Add(*it);
	}

	it = finalList.begin();
	CStringW gfile = (*it).mFilename;
	defLineOut = (*it).mLineNumber;
	if (!IsFile(gfile))
	{
		defLineOut = 0;
		gfile.Empty();
	}

	return gfile;
}

bool FindSymbolDefinition(DType* data, CStringW& outFile, int& outLine, WTString& outSym,
                          bool gotoDeclIfFail /*= true*/, bool selectSymbol /*= true*/)
{
	WrapperCheckDecoy chk(g_pUsage->mGotos);
	if (!data || !data->FileId())
		return false;

	MultiParsePtr pmp = g_currentEdCnt ? g_currentEdCnt->GetParseDb() : MultiParse::Create();
	const CStringW declFile(gFileIdManager->GetFileForUser(data->FileId()));
	if (declFile.IsEmpty())
		return false;

	if (data->IsImpl() || data->MaskedType() == GOTODEF)
	{
		outFile = declFile;
		outLine = data->Line();
		outSym = data->Sym();
		return true;
	}

	// If it is a method in project, parse opposite files looking for implementation
	if (!data->infile() && !data->IsSysLib() && data->IsMethod())
		LoadAlternateFilesForGoToDef(declFile);

	DType* def = g_pGlobDic->FindImplementation(*data);
	if (!def)
		def = GetSysDic()->FindImplementation(*data);
	if (def)
	{
		const CStringW defFile(gFileIdManager->GetFileForUser(def->FileId()));
		if (!defFile.IsEmpty())
		{
			outFile = defFile;
			outLine = def->Line();
			outSym = selectSymbol ? data->Sym() : NULLSTR;
			return true;
		}
	}

#if defined(_DEBUG) && defined(SEAN)
	// this is from the original GotoDEF in EdCnt2.cpp
	if (!data->infile() && data->IsMethod())
	{
		SymbolPosList posList;
		GetSysDic()->FindDefList(data->SymScope(), posList);
		g_pGlobDic->FindDefList(data->SymScope(), posList);
		SymbolPosList::const_iterator it = posList.begin();
		if (it != posList.end())
		{
			// def should be the first one from FindDefList
			const CStringW defFile((*it).mFilename);
			if (!defFile.IsEmpty() && IsFile(defFile))
			{
				const int line = (*it).mLineNumber;
				if (defFile != declFile && line != data->Line())
				{
					// don't assert if old way just found the declaration
					_ASSERTE(!"maybe found definition old way, NOT new way");
				}
			}
		}
	}
#endif // _DEBUG && SEAN

	if (!gotoDeclIfFail)
		return false;

	outFile = declFile;
	outLine = data->Line();
	outSym = data->Sym();
	return true;
}

bool FindAndGotoSymbolDefinition(DType* data, bool gotoDeclIfFail /*= true*/, bool selectSymbol /*= true*/)
{
	CStringW file;
	int line;
	WTString txt;
	if (FindSymbolDefinition(data, file, line, txt, gotoDeclIfFail, selectSymbol))
	{
		::VAProjectAddScopeMRU(data->SymScope().c_str(), mruMethodNav);
		DelayFileOpen(file, line, txt.c_str(), TRUE);
		return true;
	}
	return false;
}

BOOL GetGoToDEFLocation(LPCSTR sym, CStringW& file, int& line, WTString& txtToSelect)
{
	EdCntPtr ed(g_currentEdCnt);
	MultiParsePtr pmp = ed ? ed->GetParseDb() : MultiParse::Create(gTypingDevLang != -1 ? gTypingDevLang : Src);
	DType* def = pmp->FindExact2(sym);
	if (!def)
	{
		// [case: 56072]
		const WTString wtSym(sym);
		def = pmp->FindSym(&wtSym, NULL, NULL, FDF_NoConcat | FDF_GotoDefIsOk);
		if (!def || def->type() != GOTODEF)
			return FALSE;
	}
	return FindSymbolDefinition(def, file, line, txtToSelect);
}

BOOL GoToDEF(LPCSTR sym)
{
	CStringW file;
	int line;
	WTString txt;
	if (GetGoToDEFLocation(sym, file, line, txt))
	{
		DelayFileOpen(file, line, txt.c_str(), TRUE);
		return true;
	}
	return false;
}

DType* FindDeclaration(DType& data)
{
	if (!IsCFile(gTypingDevLang))
		return &data; // other languages don't have separate dec and def

	if (data.IsProtected() || data.IsPrivate() || data.IsInternal())
	{
		// if data passed in has protected or private access, then it is the declaration
		return &data;
	}

	DType* found = GetDFileMP(gTypingDevLang)->LDictionary()->FindDeclaration(data);
	if (!found)
		found = g_pGlobDic->FindDeclaration(data);
	if (!found)
	{
		EdCntPtr ed(g_currentEdCnt);
		MultiParsePtr pmp = ed ? ed->GetParseDb() : MultiParse::Create();
		found = pmp->LDictionary()->FindDeclaration(data);
		if (!found)
			found = pmp->SDictionary()->FindDeclaration(data);
	}

	if (!found || found == &data)
		return &data;
	if (found->IsPrivate() || found->IsProtected() || found->IsInternal())
		return found; // found really is the declaration

	// both found and data are public - so if we can suss out which is the dec
	if (data.MaskedType() == GOTODEF)
		return found;
	if (found->MaskedType() == GOTODEF)
		return &data;

	// hmmm - what does it mean if we get here?
	return found;
}

bool GotoSymbol(GoAction action, DType* data)
{
	bool didGo = false;
	if (!data)
		return didGo;

	if (Go_Default == action)
	{
		if (Psettings->mVaviewGotoDef)
			action = Go_Definition;
		else
			action = Go_Declaration;
	}

	if (Go_Definition == action && !data->IsImpl() && // no need to do a lookup if the data we have is the def
	    !(data->MaskedType() == GOTODEF) &&           // no need to do a lookup if the data we have is the def
	    data->IsMethod())
	{
		if (FindAndGotoSymbolDefinition(data, false))
			didGo = true;
	}

	if (Go_Declaration == action || !didGo)
		didGo = GotoSymbol(*data);

	return didGo;
}

// [case: 111093] remove _Implementation or _Validate from a method name
WTString RemoveUnrealPostfixFromImp(WTString impText)
{
	int start = impText.Find("_Implementation(");
	int count = 15;

	if (start == -1)
	{
		start = impText.Find("_Validate(");
		count = 9;
	}

	if (start != -1)
		impText.ReplaceAt(start, count, "");

	return impText;
}

WTString StripDefToBareEssentials(const WTString& s, const WTString& symName, int symType,
                                  bool ignoreVirtualAbstractOverride, bool ignoreUnrealPostfix)
{
	WTString res(s);

	// [case: 110732] create implementation erroneously offered for methods defined using UPARAM (when implementation
	// already exists)
	res = RemoveUparamFromImp(res);

	// watch out for concat'd defs
	const int nextdef = res.Find('\f');
	if (nextdef > 0)
		res = res.Mid(0, nextdef);

	// watch out for declaration vs definition
	const int elipPos = res.Find("{...}");
	if (elipPos > 0)
		res = res.Mid(0, elipPos);

	// [case: 118695][case: 104564]
	int defaultPos = res.Find("=default");
	if (defaultPos > 0)
		res = res.Mid(0, defaultPos);
	defaultPos = res.Find("= default");
	if (defaultPos > 0)
		res = res.Mid(0, defaultPos);

	// strip return type, inline and static modifiers and class name if def
	if (symName.GetLength())
	{
		int symIdx = res.Find("operator");
		if (symIdx < 0)
			symIdx = res.Find(symName.c_str());
		if (symIdx > 0)
			res = res.Mid(symIdx);
		else if (symIdx == 0)
		{
			// [case: 56065] special handling of ctors
			const WTString op("::");
			symIdx = res.Find((symName + op).c_str());
			if (symIdx == 0)
				res = res.Mid(symName.GetLength() + op.GetLength());
		}
	}

	if (ignoreVirtualAbstractOverride)
	{
		res.ReplaceAll("virtual", "", TRUE);
		res.ReplaceAll("abstract", "", TRUE);
		res.ReplaceAll("override", "", TRUE);
		res.ReplaceAll("sealed", "", TRUE);
		res.ReplaceAll("final", "", TRUE);
	}

	switch (symType)
	{
	case VAR: {
		// [case: 80653]
		// static member initialized with () need to be stripped
		// int foo(0);
		int p1 = res.Find('(');
		if (-1 != p1)
			res = res.Left(p1);
	}
	break;
	case FUNC:
	case GOTODEF:
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		// [case: 135861]
		res.ReplaceAll("__classmethod", "", TRUE);
#endif
		break;
	}

	res.ReplaceAll(" (", "(");
	res.ReplaceAll("( ", "(");
	res.ReplaceAll(" )", ")");
	res.ReplaceAll(") ", ")");
	res.ReplaceAll("(void)", "()");

	if (res.Find("()") == -1 || res.Find("()(") != -1) // watch out for operator()(args)
	{
		int equalPos = res.Find('=');
		if (equalPos != -1)
		{
			WTString tmp;
			const int parenPos = res.Find('(');
			if (parenPos != -1 && equalPos < parenPos)
			{
				tmp = res.Left(equalPos);
				tmp += '\f';
				if (res[equalPos + 1] == '=')
				{
					tmp += '\f';
					equalPos++;
				}

				tmp += res.Mid(equalPos + 1);
				res = tmp;
			}
		}

		// remove nested block comments
		int pos1 = res.Find("/ *");
		while (-1 != pos1)
		{
			int pos2 = res.Find("* /", pos1 + 3);
			if (-1 == pos2)
				break;

			res = res.Left(pos1) + res.Mid(pos2 + 3);
			pos1 = res.Find("/ *");
		}

		// remove block comments
		pos1 = res.Find("/*");
		while (-1 != pos1)
		{
			int pos2 = res.Find("*/", pos1 + 2);
			if (-1 == pos2)
				break;

			res = res.Left(pos1) + res.Mid(pos2 + 2);
			pos1 = res.Find("/*");
		}

		res.ReplaceAll("&", " & "); // so that ',' processing doesn't remove reference
		res.ReplaceAll("*", " * "); // so that ',' processing doesn't remove pointer
		res.ReplaceAll("^", " ^ "); // so that ',' processing doesn't remove handle
		res.ReplaceAll(" ,", ",");
		res.ReplaceAll(", ", ",");
		res.ReplaceAll(" )", ")");

		token t(res);
		if (res.Find('=') > 0)
		{
			res.ReplaceAll(" =", "=");
			res.ReplaceAll("= ", "=");
			t = res;

			// remove default params
			t.ReplaceAllRegex(CStringW(LR"(=[\w_\$.\-\:]+,)"), CStringW(L","));
			t.ReplaceAllRegex(CStringW(LR"(=[-\w_\$.\-\:]+,)"), CStringW(L","));
			t.ReplaceAllRegex(CStringW(LR"(=[\w_\$.\-\:]+\))"), CStringW(L")"));

			// fix spacing so that removal of param names works
			res = t.c_str();
			res.ReplaceAll(", ", ",");
			res.ReplaceAll(" ,", ",");
			res.ReplaceAll(" )", ")");
			t = res;
		}

		// remove param names but not types
		t.ReplaceAllRegex(CStringW(LR"( [\w_\$]+,)"), CStringW(L","));
		t.ReplaceAllRegex(CStringW(LR"( [\w_\$]+\))"), CStringW(L")"));
		res = t.c_str();
		res.ReplaceAll("\f", "="); // restore operator symbols

		// [case: 80653] foo::foo() : bar
		int ctorBase = res.Find("):");
		if (-1 == ctorBase)
			ctorBase = res.Find(") :");
		if (-1 != ctorBase)
			res = res.Left(ctorBase + 1);
	}

	res.ReplaceAll(" ", "");
	if (ignoreVirtualAbstractOverride)
	{
		res.ReplaceAll(")=0", ")");
		res.ReplaceAll(")PURE", ")", FALSE);
	}

	if (ignoreUnrealPostfix)
		res = RemoveUnrealPostfixFromImp(res);

	return res;
}

WTString StripScopes(WTString txt)
{
	WTString ret;
	WTString curWd;

	for (int idx = 0; idx < txt.GetLength(); ++idx)
	{
		const char ch = txt[idx];
		if (ISCSYM(ch))
			curWd += ch;
		else if (ch == ':')
			curWd += ch;
		else
		{
			if (!curWd.IsEmpty())
			{
				int pos = curWd.ReverseFind("::");
				if (-1 != pos)
					curWd = curWd.Mid(pos + 2);

				ret += curWd;
				curWd.Empty();
			}

			ret += ch;
		}
	}

	if (!curWd.IsEmpty())
		ret += curWd;

	return ret;
}

bool AreSymbolDefsEquivalent(DType& d1, DType& d2, bool ignoreVirtualAbstractOverride /*= false*/,
                             bool ignoreUnrealPostfix /*= false*/)
{
	const int bestType =
	    (d1.type() == VAR || d2.type() == VAR) ? VAR : (d1.type() == FUNC || d2.type() == FUNC ? FUNC : 0);
	WTString s1(StripDefToBareEssentials(DecodeTemplates(d1.Def()), d1.Sym(), bestType, ignoreVirtualAbstractOverride));
	WTString s2(StripDefToBareEssentials(DecodeTemplates(d2.Def()), d2.Sym(), bestType, ignoreVirtualAbstractOverride,
	                                     ignoreUnrealPostfix));
	if (s1 == s2)
		return true;

	if (d1.SymScope() == d2.SymScope())
	{
		const CStringW f1 = gFileIdManager->GetFile(d1.FileId());
		const CStringW f2 = gFileIdManager->GetFile(d2.FileId());
		const int ftype1 = GetFileType(f1);
		const int ftype2 = GetFileType(f2);

		if (IsCFile(ftype1) && IsCFile(ftype2))
		{
			if (!(IsCFile(gTypingDevLang)))
			{
				s1 = StripDefToBareEssentials(DecodeTemplates(d1.Def(), Header), d1.Sym(), bestType,
				                              ignoreVirtualAbstractOverride);
				s2 = StripDefToBareEssentials(DecodeTemplates(d2.Def(), Header), d2.Sym(), bestType,
				                              ignoreVirtualAbstractOverride);
			}

			// metro apps uses fully scoped parameters in header declaration.
			// in source file definition, with using statements, parameters are not fully scoped.
			const int scopePos1 = s1.Find("::");
			const int scopePos2 = s2.Find("::");
			if (-1 == scopePos2 && -1 != scopePos1)
			{
				s1 = StripDefToBareEssentials(DecodeTemplates(d1.Def(), Header), d1.Sym(), bestType, true);
				// strip namespace/class from s1 params
				s1 = StripScopes(s1);
			}
			else if (-1 == scopePos1 && -1 != scopePos2)
			{
				s2 = StripDefToBareEssentials(DecodeTemplates(d2.Def(), Header), d2.Sym(), bestType, true);
				// strip namespace/class from s2 params
				s2 = StripScopes(s2);
			}
			else
				return false;

			if (s1 == s2)
			{
				vLog("WARN: equiv symdefs after scope strip d1(%s) d2(%s)", d1.Def().c_str(), d2.Def().c_str());
				return true;
			}
		}
	}

	// They still might be equivalent - but things like usage of macros in
	// decl or def will make them appear unique...
	return false;
}

static thread_local int sTraverseUsingRecurseCnt1;
static thread_local int sTraverseUsingRecurseCnt2;

DType* TraverseUsing(DType* data, MultiParse* mp, bool resolveNamespaceAlias /*= false*/)
{
	if (!data)
		return NULL;

	const uint symType = data->MaskedType();
	if (TYPE == symType || FUNC == symType)
	{
		// check for "using std::vector;"    --OR--    "using BaseClass::BaseMethod;"
		// inserted into db as a using statement - see VAParseMPReparse::HandleUsingStatement
		// At this point, data might be the using statement record rather than the underlying def
		_ASSERTE(mp);
		WTString def(data->Def());
		if (def[0] == 'u' && def.Find("using ") == 0)
		{
			DType* tmpData = nullptr;
			WTString searchScope = data->Scope();
			searchScope.ReplaceAll('.', DB_SEP_CHR);

			// [case: 86670] process concat'd defs independently until a hit is made
			token2 defs = def;
			WTString targetSym;
			WTString searchSym;
			do
			{
				defs.read('\f', targetSym);
				if (targetSym.IsEmpty() || targetSym[0] != 'u' || targetSym.Find("using ") != 0)
					continue;

				targetSym.MidInPlace(6);
				targetSym.ReplaceAll('.', DB_SEP_CHR);
				targetSym.prepend(DB_SEP_STR);

				for (;;)
				{
					searchSym = {searchScope, targetSym};
					tmpData = mp->FindExact2(searchSym);
					if (tmpData)
						break;

					// break if just searched global scope
					if (searchScope.GetLength() == 0)
						break;

					searchScope.MidInPlace(::StrGetSymScope_sv(searchScope));
				}
			} while (!tmpData && defs.more());

			// using a using?
			if (tmpData)
			{
				if (sTraverseUsingRecurseCnt1 < 5)
				{
					++sTraverseUsingRecurseCnt1;
					tmpData = TraverseUsing(tmpData, mp);
					--sTraverseUsingRecurseCnt1;
				}
				else
				{
					vLog("WARN: TU1: skip recursion for %s %s", tmpData->SymScope().c_str(), tmpData->Def().c_str());
				}
			}

#ifdef case113970 // aborted
			if (!tmpData)
			{
				WTString bcl(mp->GetBaseClassList(data->Scope(), false, nullptr));
				WTString meth("get");
				DType* opData = mp->FindSym(&meth, nullptr, &bcl);
				DTypeList dats;
				g_pGlobDic->FindExactList(meth, data->ScopeHash(), dats);
				if (opData)
				{
					_asm nop;
				}
			}
#endif

			if (tmpData)
			{
				if (tmpData->IsVaForwardDeclare())
				{
					// [case: 14457] skip forward declarations
					DType* tmp2 = mp->FindExact2(def);
					if (tmp2)
						return tmp2;
				}
				return tmpData;
			}
		}
	}
	else if (resolveNamespaceAlias && NAMESPACE == symType)
	{
		// [case: 1364]
		_ASSERTE(mp);
		WTString def(data->Def());
		if (def[0] == 'n' && def.Find("namespace ") == 0)
		{
			// resolve alias
			// namespace foo = boost::somenamespace;
			int pos = def.Find('=');
			if (-1 != pos)
			{
				def.MidInPlace(pos + 1);
				def.TrimLeft();

				WTString searchScope = data->Scope();
				searchScope.ReplaceAll('.', DB_SEP_CHR);

				WTString targetSym = def;
				targetSym.ReplaceAll('.', DB_SEP_CHR);
				targetSym.ReplaceAll("::", DB_SEP_STR.c_str());
				targetSym.prepend(DB_SEP_STR);

				DType* tmpData = NULL;
				WTString searchSym;
				for (;;)
				{
					searchSym = {searchScope, targetSym};
					tmpData = mp->FindExact2(searchSym);
					if (tmpData)
						break;

					// break if just searched global scope
					if (searchScope.GetLength() == 0)
						break;

					searchScope.MidInPlace(::StrGetSymScope_sv(searchScope));
				}

				if (tmpData)
				{
					if (sTraverseUsingRecurseCnt2 < 5)
					{
						++sTraverseUsingRecurseCnt2;
						tmpData = TraverseUsing(tmpData, mp);
						--sTraverseUsingRecurseCnt2;
					}
					else
					{
						vLog("WARN: TU2: skip recursion for %s %s", tmpData->SymScope().c_str(),
						     tmpData->Def().c_str());
					}

					if (tmpData)
						return tmpData;
				}
			}
		}
	}

	return data;
}

DTypePtr TraverseUsing(DTypePtr data, MultiParse* mp, bool resolveNamespaceAlias /*= false*/)
{
	DType* foundDtype = TraverseUsing(data.get(), mp, resolveNamespaceAlias);
	if (foundDtype != data.get())
		return std::make_shared<DType>(foundDtype);
	return data;
}

void AdjustScopesForNamespaceUsings(MultiParse* m_mp, const WTString& bcl, WTString& baseScp, WTString& scp)
{
	DType* data = m_mp->FindExact(baseScp.c_str());
	if (data)
	{
		// [case: 100157]
		if (data->type() != TYPE || !data->DefStartsWith("using"))
			return;

		// using declaration (not directive)
		// using OWL::string;
		WTString usingDef(data->Def());
		usingDef = usingDef.Mid(6);
		if (!usingDef.GetLength())
			return;

		if (usingDef[0] != DB_SEP_CHR)
			usingDef = DB_SEP_STR + usingDef;
		usingDef.ReplaceAll(".", DB_SEP_STR.c_str());
		data = m_mp->FindExact(usingDef.c_str());
		if (!data)
			return;
	}
	else
	{
		// [case: 20072]
		// correct for namespace usings (using directives)
		data = m_mp->FindSym(&baseScp, &baseScp, &bcl);
		if (!data)
			return;
	}

	// [case: 76985]
	if (data->IsConstructor())
	{
		data = m_mp->FindSym(&baseScp, &baseScp, &bcl, FDF_TYPE);
		if (!data)
			return;
	}

	DTypePtr pDat = std::make_shared<DType>(data);
	if (!pDat->SymMatch(::StrGetSym(baseScp)))
		return;

	const WTString newScope(pDat->SymScope());
	if (!(baseScp != newScope && -1 != newScope.Find(baseScp.c_str())))
		return;

	int pos = scp.Find(baseScp.c_str());
	if (-1 == pos)
		return;

	const int baseScpLen = baseScp.GetLength();
	const int newScpLen = newScope.GetLength();
	if (newScpLen <= baseScpLen)
		return;

	// see if additional scp is a namespace
	WTString addlScope;
	addlScope = newScope.Left(newScpLen - baseScpLen);
	DType* strippedDat = m_mp->FindExact(addlScope.c_str());
	if (!strippedDat || NAMESPACE != strippedDat->type())
		return;

	// yes, add namespace to scopes
	scp.ReplaceAt(pos, baseScpLen, newScope.c_str());
	baseScp = newScope;
}

void DTypeList::GetStrs()
{
	for (DTypeList::iterator it = begin(); it != end(); ++it)
		(*it).LoadStrs();
}

void DTypeList::ReloadStrs()
{
	for (DTypeList::iterator it = begin(); it != end(); ++it)
		(*it).ReloadStrs();
}

void DTypeList::FilterNonActiveProjectDefs()
{
	const CStringW curFile(g_currentEdCnt ? g_currentEdCnt->FileName() : CStringW());
	if (curFile.IsEmpty())
		return;

	// [case: 67966]
	bool doFilter = false;
	const ProjectVec projForActiveFile(GlobalProject->GetProjectForFile(curFile));
	if (!projForActiveFile.size())
		return;

	if (::ContainsPseudoProject(projForActiveFile))
	{
		// [case: 68530] pseudo projects break the filter logic
		// return without filtering
		return;
	}

	DTypeList::const_iterator it;
	for (it = begin(); it != end(); ++it)
	{
		const DType& curDtype(*it);
		const ProjectVec curItemProj(GlobalProject->GetProjectForFile(curDtype.FileId()));
		if (!curItemProj.size())
		{
			// no project found for curDtype
			// curDtype is either system sym or is external
			// return without filtering
			return;
		}

		if (::ContainsPseudoProject(curItemProj))
		{
			// [case: 68530] pseudo projects break the filter logic
			// return without filtering
			return;
		}

		if (::Intersects(curItemProj, projForActiveFile))
		{
			doFilter = true;

			// don't break due to checks made for subsequent items that
			// might require return before filter.
		}
	}

	if (!doFilter)
		return;

	for (it = begin(); it != end();)
	{
		const DType& curType(*it);
		ProjectVec curItemProj(GlobalProject->GetProjectForFile(curType.FileId()));
		if (!::Intersects(curItemProj, projForActiveFile))
			erase(it++);
		else
			++it;
	}
}

void DTypeList::FilterNonActiveSystemDefs()
{
	// see also implementation of DefListFilter::FilterSystemDefs
	DTypeList::iterator it;
	DTypeList externalSysItems, sysItems;

	// split sys items into two buckets
	for (it = begin(); it != end(); ++it)
	{
		DType& curItem = *it;
		if (!(curItem.IsSystemSymbol()) || (curItem.IsManaged()))
			continue;

		const CStringW itemFile(gFileIdManager->GetFile(curItem.FileId()));
		if (IncludeDirs::IsSystemFile(itemFile))
			sysItems.push_back(curItem);
		else
			externalSysItems.push_back(curItem);
	}

	if (externalSysItems.empty() && sysItems.empty())
		return; // easy, no sys items - let all pass

	if (externalSysItems.empty())
		return; // have only sysdb items that are appropriate - let all pass

	if (sysItems.empty())
	{
		if (externalSysItems.size() != size())
		{
			// [case: 110298]
			// we have a mix of sysdb items that are not current (external) and
			// solution items.
			// global va stdafx.h can fall into this category, so check for and remove
			// va stdafx items from this.
			for (it = externalSysItems.begin(); it != externalSysItems.end(); ++it)
			{
				if (it->IsVaStdAfx())
					remove(*it);
			}
		}
		else
		{
			// have only sysdb items that are not current - let all pass
		}

		return;
	}

	// we have a mix of sysdb items, so need to filter the list of items
	// defined in files that don't come from the current set of include paths.

	// Remove items from lst that are in externalSysItems
	for (it = externalSysItems.begin(); it != externalSysItems.end(); ++it)
	{
#ifdef _DEBUG
		// [case: 65910]
		// this condition can occur when you have multiple versions of UE source in program files;
		// we filter out the versions that are not applicable here.
		const CStringW f(it->FilePath());
#endif
		remove(*it);
	}
}

void DTypeList::FilterDupesAndGotoDefs()
{
	DTypeList::iterator it1;

	// filter (global GOTODEFs) that are dupes of (local defs)
	// [case: 62542] reversed logic so that declarations are kept and GOTODEFs are tossed
	for (it1 = begin(); it1 != end(); ++it1)
	{
		for (DTypeList::iterator it = begin(); it != end();)
		{
			if (it != it1 && (*it1).FileId() == (*it).FileId() && (*it1).Line() == (*it).Line() &&
			    (*it).MaskedType() == GOTODEF)
				erase(it++);
			else
				++it;
		}
	}

	bool allGotoDefs = true;
	for (it1 = begin(); allGotoDefs && it1 != end(); ++it1)
		if ((*it1).MaskedType() != GOTODEF)
			allGotoDefs = false;

	if (!allGotoDefs)
	{
		// filter GOTODEFs (but not if there are only GOTODEFs in the list)
		for (it1 = begin(); it1 != end();)
		{
			if ((*it1).MaskedType() == GOTODEF)
				erase(it1++);
			else
				++it1;
		}
	}

	// filter dupes - same fileid and line
	for (it1 = begin(); it1 != end(); ++it1)
	{
		for (DTypeList::iterator it = begin(); it != end();)
		{
			if (it != it1 && (*it1).FileId() == (*it).FileId() && (*it1).Line() == (*it).Line())
				erase(it++);
			else
				++it;
		}
	}
}

void DTypeList::FilterDupes()
{
	DTypeList::iterator it1;

	// filter (global GOTODEFs) that are dupes of (local defs)
	// [case: 62542] reversed logic so that declarations are kept and GOTODEFs are tossed
	for (it1 = begin(); it1 != end(); ++it1)
	{
		for (DTypeList::iterator it = begin(); it != end();)
		{
			if (it != it1 && (*it1).FileId() == (*it).FileId() && (*it1).Line() == (*it).Line() &&
			    (*it).MaskedType() == GOTODEF)
				erase(it++);
			else
				++it;
		}
	}

	// filter dupes - same fileid and line
	for (it1 = begin(); it1 != end(); ++it1)
	{
		for (DTypeList::iterator it = begin(); it != end();)
		{
			if (it != it1 && (*it1).FileId() == (*it).FileId() && (*it1).Line() == (*it).Line())
				erase(it++);
			else
				++it;
		}
	}
}

void DTypeList::FilterGeneratedSourceFiles()
{
	// [case: 115343]
	if (!Psettings->mFilterGeneratedSourceFiles)
		return;

	DTypeList generatedSource;

	// iterate over results and build generatedSource
	for (const auto& it : *this)
	{
		const UINT fid = it.FileId();
		const CStringW& file(gFileIdManager->GetFile(fid));
		CStringW fileBase(::GetBaseName(file));
		int fType = ::GetFileType(fileBase);

		if (IsCFile(fType))
		{
			// #filterGeneratedSourceLiterals
			if (-1 != fileBase.Find(L".generated."))
				generatedSource.push_back(it);
			else if (Src == fType && 0 == fileBase.Find(L"moc_"))
				generatedSource.push_back(it); // Qt moc_*.cpp
			else if (Src == fType && -1 != fileBase.Find(L".gen."))
				generatedSource.push_back(it); // UE *.gen.cpp
		}
	}

	if (generatedSource.empty())
		return;

	if (generatedSource.size() == size())
	{
		// do not filter if the result set would become empty
		return;
	}

	// for each item in generated source, remove from results
	for (const auto& cur : generatedSource)
		remove(cur);
}

void DTypeList::FilterEquivalentDefsIfNoFilePath()
{
	for (auto it1 = begin(); it1 != end(); ++it1)
	{
		if (it1->FilePath().IsEmpty())
		{
			auto it2 = it1;
			++it2;
			while (it2 != end())
			{
				if (it2->FilePath().IsEmpty() && AreSymbolDefsEquivalent(*it2, *it1, true))
					erase(it2++);
				else
					++it2;
			}
		}
	}
}

void DTypeList::PurgeMissingFiles()
{
	std::set<UINT> filesToRemove;
	UINT lastFid = 0;

	for (auto it = begin(); it != end();)
	{
		bool doRemove = false;
		const UINT fid = it->FileId();
		if (fid != lastFid)
		{
			const CStringW f(gFileIdManager->GetFileForUser(fid));
			if (!f.IsEmpty())
			{
				if (!::IsFile(f))
				{
					doRemove = true;
					filesToRemove.insert(fid);
				}
			}

			lastFid = fid;
		}

		if (doRemove)
			erase(it++);
		else
			++it;
	}

	if (!filesToRemove.empty() && g_ParserThread)
		g_ParserThread->QueueParseWorkItem(new SymbolRemover(filesToRemove));
}

void DTypeList::FilterEquivalentDefs()
{
	for (DTypeList::iterator it1 = begin(); it1 != end(); ++it1)
	{
		for (DTypeList::iterator it = begin(); it != end();)
		{
			if (it != it1 && ::AreSymbolDefsEquivalent(*it, *it1))
				erase(it++);
			else
				++it;
		}
	}
}

LPCSTR
GetDefaultVisibilityForSnippetSubst(int devlang)
{
	switch (devlang)
	{
	case Src:
	case Header:
		// C/C++ snippets don't have default visibility
		return "";
	case CS:
		return "private";
	case VB:
		return "Private";
	}

	return "";
}

LPCSTR
GetVisibilityString(SymbolVisibility v, int devLang)
{
	if (!Is_C_CS_VB_File(devLang))
		return NULL;

	if (vPrivate == v)
	{
		if (VB == devLang)
			return "Private";
		return "private";
	}
	if (vProtectedInternal == v)
	{
		_ASSERTE(CS == devLang && "protected internal only valid for C#");
		return "protected internal";
	}
	if (vPrivateProtected == v)
	{
		_ASSERTE(CS == devLang && "private protected only valid for C#");
		return "private protected";
	}
	if (vInternal == v)
	{
		_ASSERTE(CS == devLang && "internal only valid for C#");
		return "internal";
	}
	if (vProtected == v)
	{
		if (VB == devLang)
			return "Protected";
		return "protected";
	}
	if (vPublic == v)
	{
		if (VB == devLang)
			return "Public";
		return "public";
	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	if (vPublished == v)
	{
		_ASSERTE(Src == devLang && "__published only valid for C++");
		return "__published";
	}
#endif

	_ASSERTE(!"unhandled visibility in GetVisibilityString");
	return NULL;
}

DType* GetBestEnumDtype(MultiParse* pmp, DType* data)
{
	if (!data || !(IsCFile(gTypingDevLang)))
		return data;

	_ASSERTE(pmp);
	_ASSERTE(data->type() == C_ENUMITEM);

	WTString def(data->Def()); // enum foo val = 1
	const WTString sym(data->Sym());
	const WTString scp(data->Scope());

	// if def contains enum XXX val, return dat that has XXX in scope
	// see if def has named enum
	int pos = def.Find(sym.c_str());
	if (-1 == pos)
		return data;

	def = def.Left(pos);
	WTString tmp("enum ");
	pos = def.Find(tmp.c_str());
	if (0 != pos)
		return data;

	def = def.Mid(pos + tmp.GetLength());
	def.Trim();
	if (def.IsEmpty())
		return data;
	if (-1 != def.Find(' '))
		return data;

	LPCTSTR p1 = strstrWholeWord(scp, def);
	if (p1)
	{
		// data already has the enum name in the scope
		return data;
	}

	// do a new dtype find using data scope and enum name in def
	WTString scopedSym(scp + DB_SEP_STR + def + DB_SEP_STR + sym);
	DType* betterDat = pmp->FindExact2(scopedSym.c_str(), false);
	if (!betterDat)
		betterDat = data;

	return betterDat;
}

DType* ResolveTypeStr(WTString type, DType* scopeDtype, MultiParse* mp)
{
	_ASSERTE(mp && scopeDtype);
	if (type.Find('.') >= 0)
		type.ReplaceAll('.', DB_SEP_CHR);
	if (type[0] != DB_SEP_CHR)
		type.insert_new(0, DB_SEP_STR.c_str());

	// instantiate template, just in case
	mp->GetBaseClassList(type);

	WTString typeSym = StrGetSym(type);
	WTString typeScope = StrGetSymScope(type);
	WTString tmpBcl = scopeDtype->Scope();
	WTString searchBcl = tmpBcl;
	if (typeScope[0] != DB_SEP_CHR)
	{
		typeScope.insert(0, DB_SEP_STR.c_str());
		if (-1 == scopeDtype->Def().Find(("::" + typeSym).c_str()))
		{
			WTString scp(tmpBcl), tmp;
			while (!scp.IsEmpty() && scp != DB_SEP_STR)
			{
				tmp = mp->GetBaseClassList(scp);
				if (tmp.IsEmpty() || tmp == "\f")
					break;

				if (-1 == tmp.Find(kWildCardScopeStr.c_str()))
				{
					searchBcl = tmp;
					tmp.ReplaceAll("\f", "");
					tmp.ReplaceAll(DB_SEP_STR.c_str(), "");
					if (tmp.IsEmpty())
						searchBcl = tmpBcl;
					break;
				}

				scp = StrGetSymScope(scp);
			}
		}
		else
		{
			// global scope
			tmpBcl.Empty();
			searchBcl.Empty();
		}
	}

	std::string_view tmpBcl2(tmpBcl);
	for (;;)
	{
		tmpBcl2 = StrGetSymScope_sv(tmpBcl2);
		if (tmpBcl2.empty() || tmpBcl2 == DB_SEP_CHR)
			break;

		searchBcl += {"\f", tmpBcl2};
	}

	DType* foundVarType = mp->FindSym(&typeSym, &typeScope, &searchBcl,
	                                  FDF_NoConcat | FDF_TYPE | FDF_SkipIgnoredFiles | FDF_NoNamespaceGuess |
	                                      FDF_SlowUsingNamespaceLogic);
	return foundVarType;
}

void SimplifySymbolScopeForFileUsings(WTString& symType, MultiParse* mp, uint fileId)
{
	_ASSERTE(mp);
	WTString remainingScope = StrGetSymScope(symType);
	if (remainingScope.IsEmpty())
		return;

	// [case: 86311]
	remainingScope += DB_SEP_STR;
	WTString fileScopedUsing;
	#ifdef _DEBUG
	static bool asserted_once = false;
	if(!fileId && !asserted_once)
	{
		_ASSERTE(fileId);
		asserted_once = true;
	}
	#endif
	fileScopedUsing.WTFormat(":wtUsingNamespace_%x", fileId);
	DTypeList dtList;
	mp->FindExactList(fileScopedUsing.c_str(), dtList, false);
	// iterate over file level namespace usings and determine how
	// much can be chopped off symType
	int chopPos = -1;
	for (auto& dt : dtList)
	{
		WTString ns(dt.Def());
		if (ns.IsEmpty())
			continue;

		// Note: Ideally, we would skip items in list whose line number comes
		// after the line number that we are working on (if the line number
		// were passed into this function)
		// (see changes 26136/26141 for a test added at end of file that caused
		// tests higher up in the file to start failing because of a module-level
		// using statement being added at bottom of file).

		if (dt.FileId() == fileId && dt.MaskedType() == RESWORD && dt.Attributes() & V_IDX_FLAG_USING)
		{
			if (ns[0] == DB_SEP_CHR)
			{
				int pos = remainingScope.Find((ns + DB_SEP_STR).c_str());
				if (0 == pos)
				{
					pos = ns.GetLength();
					if (pos > chopPos)
						chopPos = pos;
				}
			}
		}
	}

	if (-1 != chopPos)
	{
		symType = symType.Mid(chopPos);
		_ASSERTE(!symType.IsEmpty() && symType[0] == DB_SEP_CHR);
	}
}

void QualifyTypeWithFullScope(WTString& symType, DType* symDef, MultiParse* mp, const WTString& localScope,
                              const WTString& symHintScope)
{
	_ASSERTE(symDef && mp);
	DType* rdt = ResolveTypeStr(symType, symDef, mp);
	if (rdt)
	{
		const WTString newSymScp(rdt->SymScope());
		if (symType.GetLength() < newSymScp.GetLength() &&
		    newSymScp.EndsWith(symType[0] == DB_SEP_CHR ? symType : DB_SEP_STR + symType))
		{
			symType = newSymScp;

			// remove outer scope that duplicates current scope
			WTString curScope(localScope);
			for (;;)
			{
				WTString outerScp = StrGetOuterScope(symType);
				if (!outerScp.IsEmpty() && StartsWith(curScope, outerScp + DB_SEP_STR, FALSE))
				{
					symType = symType.Mid(outerScp.GetLength());
					curScope = curScope.Mid(outerScp.GetLength());
				}
				else
					break;
			}

			if (!symHintScope.IsEmpty() && curScope == localScope && symHintScope[0] == DB_SEP_CHR)
			{
				// [case: 86415]
				// nothing removed and we have a hint scope; see if hint is a derived class
				WTString hint(symHintScope);
				if (-1 == localScope.Find(hint.c_str()) && -1 == symType.Find(hint.c_str()))
				{
					const WTString sym(StrGetSym(symType));
					const WTString bcl(mp->GetBaseClassList(hint));
					WTString tmp(bcl);
					tmp.ReplaceAll(("\f" + hint + "\f").c_str(), "");
					tmp.ReplaceAll("\f", "");
					tmp.ReplaceAll(DB_SEP_STR.c_str(), "");
					if (!tmp.IsEmpty())
					{
						DType* cd = mp->FindSym(&sym, &hint, &bcl,
						                        FDF_NoConcat | FDF_NoNamespaceGuess | FDF_IgnoreGlobalNamespace);
						if (cd)
						{
							// [case: 86849]
							const WTString cdScope(cd->Scope());
							if (!cdScope.IsEmpty() && cdScope != DB_SEP_STR)
							{
								// given size_type, return std::string::size_type instead of
								// basic_string<....::size_type
								if (hint[hint.GetLength() - 1] != DB_SEP_CHR)
									hint += DB_SEP_CHR;
								symType = hint + sym;
							}
						}
					}
				}
			}

			UINT fid = mp->GetFileID();
			SimplifySymbolScopeForFileUsings(symType, mp, fid);
		}
	}
}

// CombineOverlappedScope
// ------------------------------------------------------------------------
// Given:
//	baseScope of			:Ns:Base:Derived:exec
//	unqualifiedScope of				:Derived:exec-1:foo
//	return					:Ns:Base:Derived:exec-1:foo
//	if no overlap, then return empty string
//
WTString CombineOverlappedScope(const WTString& baseScope, const WTString& unqualifiedScope)
{
	if (baseScope.IsEmpty() || unqualifiedScope.IsEmpty() || baseScope == unqualifiedScope)
		return {};

	if (baseScope[0] != DB_SEP_CHR || unqualifiedScope[0] != DB_SEP_CHR ||
	    baseScope[baseScope.GetLength() - 1] == DB_SEP_CHR ||
	    unqualifiedScope[unqualifiedScope.GetLength() - 1] == DB_SEP_CHR)
		return {};

	int posToCheck = 0;

	std::string_view ret_sv;
	for (;;)
	{
		posToCheck = baseScope.Find(DB_SEP_STR.c_str(), posToCheck);
		if (-1 == posToCheck)
			break;

		std::string_view tmp = baseScope.mid_sv((uint32_t)posToCheck);
		if (tmp.empty())
			break;

		if (StartsWith(unqualifiedScope.to_string_view(), tmp))
			ret_sv = baseScope.left_sv((uint32_t)posToCheck);

		++posToCheck;
	}

	if (!ret_sv.empty())
		return {ret_sv, unqualifiedScope};
	else
		return {};
}

static bool IsSymOperator(DType* dt)
{
    return dt->MaskedType() == FUNC && strstrWholeWord(dt->Def(), "operator") && !strstrWholeWord(dt->Sym(), "operator");
}
WTString GetSymOperatorSafe(DType* dt)
{
	// some operator Sym's are stored as "+=", and others as "operator double".
	// This will return "operator +=" and "operator double"
	if (IsSymOperator(dt))
		return "operator " + dt->Sym();
	else
		return dt->Sym();
}

WTString GetSymScopeOperatorSafe(DType* dt)
{
	return dt->Scope() + DB_SEP_STR + GetSymOperatorSafe(dt);
}
void GetSymScopeOperatorSafe_dump(DType* dt, std::string &out)
{
// the same as GetSymScopeOperatorSafe, but formats the output in provided string instance
	out = dt->Scope_sv().first;
	out.append(DB_SEP_STR);

	if (IsSymOperator(dt))
		out.append("operator ");
	out.append(dt->Sym_sv().first);
}

// This function calculates the probability (expressed as a percentage) that one or two lists are sorted by chance,
// based on the total number of items in each list and the concept of permutations. 
// It aids in discerning whether the sorting of a list (or lists) appears intentional or coincidental,
// guiding the decision on where new includes should be added.
//
// If you set mAddIncludeSortedMinProbability to 0, it will always return false.
// If you set mAddIncludeSortedMinProbability to 100, it will always return true.
bool IsSortedOrderLikelyNotPureChance(int ItemsInFirstList, int ItemsInSecondList)
{
	auto factorial = [](int n) {
		float result = 1;
		for (int i = 2; i <= n; ++i)
			result *= static_cast<float>(i);
		return result;
	};

	float probabilityFirstList = 1.0f / factorial(ItemsInFirstList);
	float probabilitySecondList = 1.0f / factorial(ItemsInSecondList);

	float combinedProbability = probabilityFirstList * probabilitySecondList;

	return combinedProbability * 100 <= static_cast<float>(Psettings->mAddIncludeSortedMinProbability);
}

// Let's start the string from the filename to avoid comparing < with "
CStringW GetIncludeFromFileNameLower(CStringW text)
{
	int pos = text.FindOneOf(L"\"<");
	if (pos != -1)
	{
		text = text.Mid(pos + 1);
	}
	text.MakeLower();
	return text;
};

#ifdef REMOTEHEAP_COLLECT_STATS
#include "..\common\ConsoleWnd.h"
#include <variant>
extern "C" WINBASEAPI DWORD WINAPI GetThreadId(__in HANDLE Thread);
namespace remote_heap::stats
{
void ManageRemoteHeapStatsConsole(bool open)
{
	static std::unique_ptr<std::thread> t;
	static std::mutex m;
	if (!open)
	{
		if (t)
		{
			::PostThreadMessage(::GetThreadId(t->native_handle()), WM_QUIT, 0, 0);
			m.unlock();
			t->join();
		}
		return;
	}
	if (t)
		return;

	m.lock();
	t = std::make_unique<std::thread>([] {
		for (char c = '1'; c <= '6'; c++)
		{
			if (::RegisterHotKey(nullptr, 1, MOD_ALT | MOD_CONTROL | 0x4000 /*MOD_NOREPEAT*/, (UINT)c))
				break;
		}
		auto start_time = std::chrono::steady_clock::now();

		MSG msg = {0};
		int hotkeycnt = 0;
		static const int presses_needed = 2;
		while (::GetMessage(&msg, nullptr, 0, 0) != 0)
		{
			if ((msg.message == WM_HOTKEY) && (msg.wParam == 1))
			{
				if (++hotkeycnt == presses_needed)
					break;
			}
		}

		if (hotkeycnt == presses_needed)
		{
			size_t cwidth = /*80*/ 150;
			size_t cheight = /*25*/ 53;
			ConsoleWnd sConsole("VA RemoteHeap stats", 80, 25);

			auto update_console_size = [&sConsole, &cwidth, &cheight]() {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				::GetConsoleScreenBufferInfo(sConsole, &csbi);
				SMALL_RECT sr = csbi.srWindow;
				sr.Bottom = (short)((int)sr.Top + cheight - 1);
				sr.Right = (short)((int)sr.Left + cwidth - 1);
				::SetConsoleWindowInfo(sConsole, true, &sr);
			};
			update_console_size();

			sConsole.Clear();

			enum class menustate_e
			{
				top,
				browse
			};
			auto menustate = menustate_e::top;
			bool paused = false;
			bool do_one_update = false;
			int update = 0;
			std::string last_text, text;

			size_t area_size_index = 0;
			size_t area_index = 0;
			size_t area_block_offset = 0;
			const size_t area_blocks_per_view = 8;

			while (!m.try_lock())
			{
				if (!paused || do_one_update)
				{
					size_t max_printed_width = 1;
					text.clear();

					switch (menustate)
					{
					case menustate_e::top: {
						auto stats = get_stats_copy();

						using columndef_t = std::tuple<const char*, std::variant<reason, stat, int64_t>,
						                               std::optional<std::variant<reason, stat, int64_t>>>;
						using linedef_t = std::vector<columndef_t>;
						using blockdef_t = std::vector<linedef_t>;

						/*static*/ auto get_value = [](auto a) -> size_t {
							if (auto pval = std::get_if<remote_heap::stats::reason>(&a))
								return (size_t)*pval;
							else if (auto pval2 = std::get_if<remote_heap::stats::stat>(&a))
								return (size_t)*pval2;
							else if (auto pval3 = std::get_if<int64_t>(&a))
								return (size_t)*pval3;
							else
							{
								assert(!"variant changed!");
								return 0u;
							}
						};
						auto do_block = [&stats, &max_printed_width, &get_value](std::string title,
						                                                         const blockdef_t& b) {
							using columnwidths_t = std::unordered_map<size_t, size_t>;
							using alltext_t = std::map<std::tuple<size_t, size_t>, std::string>;

							columnwidths_t columnwidths;
							alltext_t alltext;

							for (size_t l = 0; l < b.size(); l++)
							{
								for (size_t c = 0; c < b[l].size(); c++)
								{
									// TODO3264: compiler bug?! report!
									//									const auto &[name, value, avg] = b[l][c];
									const auto& name = std::get<0>(b[l][c]);
									const auto& value = std::get<1>(b[l][c]);
									const auto& avg = std::get<2>(b[l][c]);

									char temp[512];
									if (avg && std::holds_alternative<int64_t>(*avg))
									{
										// scale
										int64_t val = (int64_t)stats[get_value(value)];
										int64_t scale = (int64_t)get_value(*avg);
										assert(scale);
										sprintf_s(temp, "%lld", val / scale);
									}
									else if (avg)
									{
										// average
										int64_t cntval = stats[get_value(*avg)];
										if (cntval != 0)
										{
											int64_t val = stats[get_value(value)];
											sprintf_s(temp, "%dus avg / %lldms total / %lld cnt", int(val / cntval),
											          val / 1000, cntval);
										}
										else
											strcpy_s(temp, "---");
									}
									else
										sprintf_s(temp, "%lld", stats[get_value(value)]);

									columnwidths[c * 2] = std::max(strlen(name), columnwidths[c * 2]);
									alltext[{l, c * 2}] = name;
									columnwidths[c * 2 + 1] = std::max(strlen(temp), columnwidths[c * 2 + 1]);
									alltext[{l, c * 2 + 1}] = temp;
								}
							}

							std::string ret;
							if (!title.empty())
								ret += "\n +++ " + title + " +++ \n";
							for (size_t l = 0; l < b.size(); l++)
							{
								size_t size_at_beginning = ret.size();
								for (size_t c = 0; c < columnwidths.size(); c++)
								{
									if (c % 2)
										ret += " "; // between name and value
									else if (c >= 2)
										ret += " | "; // between two values

									const auto& str = alltext[{l, c}];
									size_t pad = columnwidths[c] - str.size();
									if ((pad > 0) && (c % 2))
										ret += std::string(pad, ' '); // align right
									ret += str;
									if ((pad > 0) && !(c % 2))
										ret += std::string(pad, ' '); // align left
								}
								max_printed_width = std::max(ret.size() - size_at_beginning, max_printed_width);
								ret += "\n";
							}
							return ret;
						};

						text += do_block("", {{{"DTypes (alive)", stat::dtypes, {}},
						                       {"defobjs", stat::defobjs, {}},
						                       {"moves", stat::dtypes_move, {}},
						                       {"copies", stat::dtypes_copy, {}},
						                       {"ctor_had_no_strings", stat::dtypes_had_no_strings, {}},
						                       {"memory (kb)", stat::dtypes_bytes, {1024}}}});

						text += do_block(
						    "Usage reasons",
						    {{{"construct", reason::me, {}},
						      {"discard", reason::discard, {}},
						      {"get_used_blocks", reason::get_used_blocks, {}},
						      {"make_copy", reason::make_copy, {}},
						      {"make_empty", reason::make_empty, {}}},
						     {
						         //								{"get_ScopeHash", reason::get_ScopeHash, {}},
						         //								{"get_SymHash", reason::get_SymHash, {}},
						         //								{"mScopeHash (local)", reason::mScopeHash, {}},
						         //								{"mSymHash (local)", reason::mSymHash, {}},
						         //								{"get_FileId", reason::get_FileId, {}},
						         //								{"get_Line", reason::get_Line, {}},
						         //								{"get_mLine", reason::get_mLine, {}}
						         //							}, {
						         //								{"get_GetDbSplit", reason::get_GetDbSplit, {}},
						         //								{"get_GetDbOffset", reason::get_GetDbOffset, {}},
						         //								{"HasData", reason::HasData, {}},
						         //								{"HasDef", reason::HasDef, {}},
						         {"HasSameSymData", reason::HasSameSymData, {}},
						         {"SetStrs", reason::SetStrs, {}},
						         {"GetStrs", reason::GetStrs, {}},
						         {"DoGetStrs", reason::DoGetStrs, {}},
						         {"SetDef", reason::SetDef, {}},
						         //							}, {
						         //								{"mTypeAndDbFlags (local)", reason::mTypeAndDbFlags,
						         //{}},
						         //								{"mAttributes (local)", reason::mAttributes, {}}
						     }});

						text += do_block("Areas",
						                 {{{"areas", stat::areas, {}},
						                   {"max", stat::areas_max, {}},
						                   {"largefilemaps", stat::areas_largefilemap, {}}},
						                  {{"mapped", stat::areas_mapped, {}},
						                   {"max", stat::areas_mapped_max, {}},
						                   {"dealloc lock retries", stat::dealloc_lock_retries, {}}},
						                  {{"lru unmap pending", stat::areas_lru_unmap_pending, {}},
						                   {"max", stat::areas_lru_unmap_pending_max, {}}},
						                  {
						                      {"mapped placeholders (total)", stat::areas_mapped_placeholder_total, {}},
						                      {"current", stat::areas_mapped_placeholder, {}},
						                      {"max", stat::areas_mapped_placeholder_max, {}},
						                      {"mapped classic (total)", stat::areas_mapped_classic_total, {}},
						                      {"current", stat::areas_mapped_classic, {}},
						                      {"max", stat::areas_mapped_classic_max, {}},
						                  }});

						static_assert(remote_heap::area_sizes == 9); // update stats if fails
						text += do_block("Areas pools", {{
						                                     {" 256: full", stat::areas_pool_full_x + 0, {}},
						                                     {"partial", stat::areas_pool_partial_x + 0, {}},
						                                     {"empty", stat::areas_pool_empty_x + 0, {}},
						                                     {" 512: full", stat::areas_pool_full_x + 1, {}},
						                                     {"partial", stat::areas_pool_partial_x + 1, {}},
						                                     {"empty", stat::areas_pool_empty_x + 1, {}},
						                                     {"1024: full", stat::areas_pool_full_x + 2, {}},
						                                     {"partial", stat::areas_pool_partial_x + 2, {}},
						                                     {"empty", stat::areas_pool_empty_x + 2, {}},
						                                 },
						                                 {
						                                     {"2048: full", stat::areas_pool_full_x + 3, {}},
						                                     {"partial", stat::areas_pool_partial_x + 3, {}},
						                                     {"empty", stat::areas_pool_empty_x + 3, {}},
						                                     {"4096: full", stat::areas_pool_full_x + 4, {}},
						                                     {"partial", stat::areas_pool_partial_x + 4, {}},
						                                     {"empty", stat::areas_pool_empty_x + 4, {}},
						                                     {"8192: full", stat::areas_pool_full_x + 5, {}},
						                                     {"partial", stat::areas_pool_partial_x + 5, {}},
						                                     {"empty", stat::areas_pool_empty_x + 5, {}},
						                                 },
						                                 {
						                                     {"16384: full", stat::areas_pool_full_x + 6, {}},
						                                     {"partial", stat::areas_pool_partial_x + 6, {}},
						                                     {"empty", stat::areas_pool_empty_x + 6, {}},
						                                     {"32768: full", stat::areas_pool_full_x + 7, {}},
						                                     {"partial", stat::areas_pool_partial_x + 7, {}},
						                                     {"empty", stat::areas_pool_empty_x + 7, {}},
						                                     {"65536: full", stat::areas_pool_full_x + 8, {}},
						                                     {"partial", stat::areas_pool_partial_x + 8, {}},
						                                     {"empty", stat::areas_pool_empty_x + 8, {}},
						                                 }}) +
						        do_block("", {{
						                         {"mapped", stat::areas_pool_mapped, {}},
						                         {"lru", stat::areas_pool_lru, {}},
						                         {"efficiency:mapped", stat::areas_pool_efficiency_alread_mapped, {}},
						                         {"lru", stat::areas_pool_efficiency_lru, {}},
						                         {"need to map", stat::areas_pool_efficiency_need_to_map, {}},
						                     }});

						text += do_block("Transitions",
						                 {{{"empty->partial", stat::areas_pool_empty_to_partial_transitions, {}},
						                   {"partial->empty", stat::areas_pool_partial_to_empty_transitions, {}},
						                   {"partial->full", stat::areas_pool_partial_to_full_transitions, {}}},
						                  {{"full->partial", stat::areas_pool_full_to_partial_transitions, {}},
						                   {"expunged from lru", stat::areas_pool_expunged_from_lru, {}}}});

						text += do_block("Blocks",
						                 {{{"blocks", stat::blocks, {}},
						                   {"max", stat::blocks_max, {}},
						                   {"available", stat::blocks_available, {}},
						                   {"memory (Mb)", stat::blocks_bytes, {1048576}}},
						                  {{"mapped", stat::blocks_mapped, {}}, {"max", stat::blocks_mapped_max, {}}},
						                  {{"allocations", stat::blocks_total_allocations, {}},
						                   {"find empty iterations", stat::blocks_total_allocation_iterations, {}}},
						                  {{"reads", stat::blocks_reads, {}},
						                   {"kbytes", stat::blocks_read_bytes, {1024}},
						                   {"writes", stat::blocks_writes, {}},
						                   {"kbytes", stat::blocks_write_bytes, {1024}}}});

						text += do_block("sdata_map", {{
						                                  {"block reused", stat::sdatamap_block_reused, {}},
						                                  {"not reused", stat::sdatamap_block_not_reused, {}},
						                              }});

						text += do_block(
						    "Timing",
						    {{{"CreateFileMapping", stat::timing_CreateFileMapping_us,
						       stat::timing_CreateFileMapping_cnt},
						      {"DType_copy", stat::timing_DType_copy_us, stat::timing_DType_copy_cnt}},
						     {{"MapViewOfFile", stat::timing_MapViewOfFile_us, stat::timing_MapViewOfFile_cnt},
						      {"UnmapViewOfFile", stat::timing_UnmapViewOfFile_us, stat::timing_UnmapViewOfFile_cnt}},
						     {{"addstrings", stat::timing_sdatamap_addstrings_us, stat::timing_sdatamap_addstrings_cnt},
						      {"copyfrom", stat::timing_sdatamap_copyfrom_us, stat::timing_sdatamap_copyfrom_cnt}},
						     {
						         {"mapped_block constructor", stat::timing_mapped_block_constructor_us,
						          stat::timing_mapped_block_constructor_cnt},
						         {"mapped_block destructor", stat::timing_mapped_block_destructor_us,
						          stat::timing_mapped_block_destructor_cnt},
						     },
						     {
						         {"GetStrs", stat::timing_GetStrs_us, stat::timing_GetStrs_cnt},
						         {"DBOut", stat::timing_DBOut_us, stat::timing_DBOut_cnt},
						     }});

						text += do_block("cs", {
						                           {
						                               {"area", stat::timing_area_cs_us, stat::timing_area_cs_cnt},
						                               {"waiting", stat::area_cs_waiting, {}},
						                               {"max waiting", stat::area_cs_waiting_max, {}},
						                           },
						                           {
						                               {"heap", stat::timing_heap_cs_us, stat::timing_heap_cs_cnt},
						                               {"waiting", stat::heap_cs_waiting, {}},
						                               {"max waiting", stat::heap_cs_waiting_max, {}},
						                           },
						                           {
						                               {"heap_mapped", stat::timing_heap_mapped_cs_us,
						                                stat::timing_heap_mapped_cs_cnt},
						                               {"waiting", stat::heap_mapped_cs_waiting, {}},
						                               {"max waiting", stat::heap_mapped_cs_waiting_max, {}},
						                           },
						                           {
						                               {"read wait", stat::read_wait, {}},
						                               {"write wait", stat::write_wait, {}},
						                           },
						                       });
					}
					break;
					case menustate_e::browse: {
						assert(area_size_index < rheap->areas.size());
						remote_heap::remote_heap_impl::area_lists& al = rheap->areas[area_size_index];

						critical_section_lock_t mapped_lck(rheap->mapped_cs);
						critical_section_lock_t arealist_lck(al.cs);
						remote_heap::remote_heap_impl::area* a = nullptr;
						if (area_index < al.full.size())
							a = *std::next(al.full.begin(), (int)area_index);
						else if ((area_index - al.full.size()) < al.partial.size())
							a = *std::next(al.partial.begin(), int(area_index - al.full.size()));

						text += string_format("area size: %zu(%zu bytes); area index: %zu/(%zu full + %zu partial); ",
						                      area_size_index, min_block_size << area_size_index, area_index,
						                      al.full.size(), al.partial.size());
						if (!a)
							text += "no area!!";
						else
						{
							critical_section_lock_t area_lck(a->cs);
							text += string_format("used blocks: %zu; free blocks: %zu; total blocks %zu\n\n",
							                      a->used_blocks, a->free_blocks, a->total_blocks);

							for (size_t b = 0; b < area_blocks_per_view; b++)
							{
								size_t block = area_block_offset + b;
								text += string_format("  block %zu: ", block);
								if (block >= a->total_blocks)
									text += "out of range!\n";
								else if (a->is_block_free(block,
								                          have_locks<lock_types::area_list | lock_types::area>{}))
									text += "not allocated\n";
								else
								{
									char buffer[max_block_size];
									memset(buffer, 0, a->block_size);
									mapped_block(a, block).get(buffer, 0, a->block_size);

									const sdata* sd = (const sdata*)buffer;
									text += string_format("total_strings_size(%d)\n", sd->total_strings_size);
									if (sd->mSymData.HasScope())
									{
										text += "    scope: ";
										text += sd->mSymData.GetSymScope().c_str();
										text += "\n";
									}
									if (sd->mSymData.HasDef())
									{
										text += "    def: ";
										text += sd->mSymData.GetDef().c_str();
										text += "\n";
									}
								}
							}
						}
					}
					break;
					}

					if (do_one_update || (last_text != text))
					{
						last_text = text;
						++update;
						auto current_time = std::chrono::steady_clock::now();
						auto uptime_s =
						    std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
						time_t tt = time(nullptr);
						std::string t2 = asctime(localtime(&tt));
						t2.pop_back(); // remove endline

						std::string help;
						switch (menustate)
						{
						case menustate_e::top:
							help = "help: (p) for pause; (c) for clear counters; (r) for reset uptime; (l) for flush "
							       "lru; (b) for blocks walk";
#ifdef _DEBUG
							help += "; (f) for mapping fail";
#endif
							if (paused)
								help += "; (u) for one update";
							break;
						case menustate_e::browse:
							help = "help: (,.) for area sizes; (left right) for area index; (up down) for block offset "
							       "within area; (x) for exit to stats";
							break;
						}

						sConsole.Clear();
						printf("%s (%s) : uptime %dmin %ds; update %d%s%s\n\n", t2.c_str(), CONFIGURATIONNAME,
						       int(uptime_s / 60), int(uptime_s % 60), update, paused ? " (paused)" : "",
#if defined(_DEBUG) || defined(VA_CPPUNIT)
						       fail_next_filemap ? " (fail filemaps)" : ""
#else
						       ""
#endif
						);
						printf("%s", text.c_str());
						printf("\n%s\n", help.c_str());

						static const size_t a_bit_more = 3;
						max_printed_width = std::max(help.size(), max_printed_width) + a_bit_more;
						size_t lines_printed = 2 + std::count(text.cbegin(), text.cend(), '\n') + 2 + a_bit_more;
						if ((max_printed_width > cwidth) || (lines_printed > cheight))
						{
							cwidth = max_printed_width;
							cheight = lines_printed;
							//							update_console_size();
							// this didn't worked too well
						}
					}
					do_one_update = false;
				}

				while (true)
				{
					DWORD events = 0;
					::GetNumberOfConsoleInputEvents(::GetStdHandle(STD_INPUT_HANDLE), &events);
					if (!events)
						break;

					events = 0;
					INPUT_RECORD ir = {0};
					::ReadConsoleInput(::GetStdHandle(STD_INPUT_HANDLE), &ir, 1, &events);
					if (!events)
						break;
					if (ir.EventType == KEY_EVENT)
					{
						if (ir.Event.KeyEvent.bKeyDown)
						{
							switch (menustate)
							{
							case menustate_e::top:
								switch (ir.Event.KeyEvent.uChar.AsciiChar)
								{
								case 'p':
									paused = !paused;
									do_one_update = true;
									break;
								case 'c':
									clear_stats();
									do_one_update = true;
									break;
								case 'u':
									do_one_update = true;
									break;
								case 'r':
									start_time = std::chrono::steady_clock::now();
									break;
								case 'l':
									rheap->cleanup_lru(have_locks<lock_types::none>(), 0);
									do_one_update = true;
									break;
#if defined(_DEBUG) || defined(VA_CPPUNIT)
								case 'f':
									fail_next_filemap = !fail_next_filemap;
									do_one_update = true;
									break;
#endif
								case 'b':
									menustate = menustate_e::browse;
									do_one_update = true;
									break;
								}
								break;
							case menustate_e::browse:
								switch (ir.Event.KeyEvent.uChar.AsciiChar)
								{
								default:
									switch (ir.Event.KeyEvent.wVirtualKeyCode)
									{
									case VK_LEFT:
										if (area_index > 0)
										{
											--area_index;
											do_one_update = true;
										}
										break;
									case VK_RIGHT:
										++area_index;
										do_one_update = true;
										break;
									case VK_UP:
										area_block_offset -= std::min(area_block_offset, area_blocks_per_view);
										do_one_update = true;
										break;
									case VK_DOWN:
										area_block_offset += area_blocks_per_view;
										do_one_update = true;
										break;
									}
									break;
								case ',':
									area_size_index = (area_size_index > 0) ? (area_size_index - 1) : (area_sizes - 1);
									do_one_update = true;
									break;
								case '.':
									area_size_index = (area_size_index + 1) % area_sizes;
									do_one_update = true;
									break;
								case 'x':
									menustate = menustate_e::top;
									do_one_update = true;
									break;
								}
								break;
							}
						}
					}
				}

				if (!do_one_update)
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}
		}
	});
}
} // namespace remote_heap::stats
#endif
#ifdef REMOTEHEAP_SHOW_LEAKS
void remote_heap::remote_heap_impl::dump_leaks(const area_lists& al)
{
	char temp[4096];
	std::string temp2;

	sprintf_s(temp, "area with leaks (%d:%d bytes)\n", al.size_index, min_block_size << al.size_index);
	temp2 += temp;

	size_t leaked = 0;
	for (auto* a2 : al.partial)
		leaked += a2->get_used();
	if (!leaked)
		return;
	sprintf_s(temp, "leaked blocks: %d\n", leaked);
	temp2 += temp;

	for (auto* a2 : al.partial)
	{
		if (!a2->get_used())
			continue;
		for (size_t block = 0; block < a2->total_blocks; block++)
		{
			const auto& bitmap = a2->get_bitmap();
			size_t bit = block % (sizeof(bitmap[0]) * 8);
			const uint64_t& bitmap_ref = bitmap[block / sizeof(bitmap[0]) / 8];
			if (!(bitmap_ref & (1ull << bit)))
				continue;

			auto* sd = (const sdata*)((const char*)a2->map(have_locks<lock_types::area>()) + block * a2->block_size);
			sprintf_s(temp, "%d. %s\n", block,
			          sd->contains_strings() ? sd->mSymData.GetDef().c_str() : "<<<no strings>>>");
			temp2 += temp;
		}
	}

	::MessageBoxA(nullptr, temp2.c_str(), "Leaks!!", MB_SYSTEMMODAL | MB_OK | MB_ICONEXCLAMATION);
}
#endif
#ifdef NO_LTALLOC
#include "ltalloc/ltalloc_va.h"
class LTAllocVA_no_remote_heap final : public ILTAllocVA
{
  public:
	~LTAllocVA_no_remote_heap() = default;

	void* ltmalloc(size_t size) override
	{
		assert(rheap);
		return rheap ? rheap->ltheap.Alloc(size) : nullptr;
	}
	void ltfree(void* p) override
	{
		assert(rheap);
		if (rheap && p)
			rheap->ltheap.Free(p);
	}
	void ltsqueeze(size_t padsz = 0) override
	{
		// no need to call HeapCompact as it is implicitly called on every HeapFree
		(void)padsz;
	}
};

void* get_iltava()
{
	static LTAllocVA_no_remote_heap ltalloc;
	return &ltalloc;
}
#endif
