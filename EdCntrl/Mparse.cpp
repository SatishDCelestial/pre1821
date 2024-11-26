#include "stdafxed.h"
#include "edcnt.h"
#include "mparse.h"
#include "foo.h"
#include "resource.h"
#include "import.h"
#include "macroList.h"
#include "DBLock.h"
#include "VAParse.h"
#include "BaseClassList.h"
#include "DevShellAttributes.h"
#include "DBFile/VADBFile.h"
#include "ParseThrd.h"
#include "FileTypes.h"
#include "file.h"
#include "StatusWnd.h"
#include "wt_stdlib.h"
#include "RecursionClass.h"
#include "StackSpace.h"
#include "RegKeys.h"
#include "Settings.h"
#include "DatabaseDirectoryLock.h"
#include "FileId.h"
#include "WtException.h"
#include "SpellBlockInfo.h"
#include "FileLineMarker.h"
#include "Directories.h"
#include "Usage.h"
#include "WrapCheck.h"
#include "myspell\WTHashList.h"
#include "Lock.h"
#include "CFileW.h"
#include "TokenW.h"
#include "FileList.h"
#include "VAHashTable.h"
#include "SpellCheckDlg.h"
#include "FileFinder.h"
#include "..\common\ThreadStatic.h"
#include "LogElapsedTime.h"
#include "..\common\TempAssign.h"
#include "DevShellService.h"
#include "CodeGraph.h"
#include "includesDb.h"
#include "SpinCriticalSection.h"
#include "inheritanceDb.h"
#include "RadStudioPlugin.h"
#include "GetFileText.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if _MSC_VER > 1200
using std::ifstream;
using std::ios;
using std::ofstream;
#endif

using OWL::string;
using OWL::TRegexp;

volatile BOOL g_WTL_FLAG = FALSE;

SpellBlockInfo s_SpellBlockInfo;
static ThreadStatic<WTString> sLastXref;
static ThreadStatic<WTString> sLastScope;
static ThreadStatic<WTString> sLastBcl1;
static ThreadStatic<WTString> sLastBcl2;
static ThreadStatic<int> sDeep;

FileList sActiveDfileList;
CSpinCriticalSection sActiveDfileListLock;
FileList sActiveFormatFileList;
CSpinCriticalSection sActiveFormatFileListLock;

static void DoParseGlob(LPVOID file);

void ClearMpStatics()
{
	sLastXref.Clear();
	sLastScope.Clear();
	sLastBcl1.Clear();
	sLastBcl2.Clear();
	sDeep.Clear();
}

LPCSTR StrGetSym(LPCSTR symscope)
{
	LPCSTR p = symscope;
	if (p)
	{
		char ch;
		while ((ch = *symscope++) != '\0')
		{
			if (DB_SEP_CHR == ch || '.' == ch)
				p = symscope;
		}
	}
	return p;
}
std::string_view StrGetSym_sv(std::string_view symscope)
{
	auto i = symscope.find_last_of(DB_SEP_CHR);
	if(i != std::string_view::npos)
		symscope.remove_prefix(i + 1);
	i = symscope.find_last_of('.');
	if (i != std::string_view::npos)
		symscope.remove_prefix(i + 1);
	return symscope;
}

WTString StrGetSymScope(LPCSTR symscope)
{
	if (symscope)
	{
		LPCSTR p = StrGetSym(symscope) - 1;
		if (p > symscope)
			return WTString(symscope, ptr_sub__int(p, symscope));
	}
	return NULLSTR;
}
std::string_view StrGetSymScope_sv(LPCSTR symscope)
{
	if (symscope)
	{
		LPCSTR p = StrGetSym(symscope) - 1;
		if (p > symscope)
			return {symscope, p};
	}
	return {};
}
std::string_view StrGetSymScope_sv(std::string_view symscope)
{
	std::string_view sv = StrGetSym_sv(symscope);
	if (!sv.empty()) [[likely]]
	{
		const char* p = sv.data() - 1;
		if (p > symscope.data())
			return {symscope.data(), p};
	}
	return {};
}

WTString StrGetOuterScope(const WTString& symscope)
{
	if (!symscope.IsEmpty() && symscope[0] == DB_SEP_CHR)
	{
		int pos = symscope.Find(DB_SEP_STR, 1);
		if (-1 != pos)
			return symscope.Left(pos);
	}

	return NULLSTR;
}

static const WTString kDefaultNamespaces(":std\f:WTL\f:ATL\f:_com_util\f:rel_ops\f"); // put WTL before ATL;;
WTString MultiParse::s_SrcNamespaces(kDefaultNamespaces);
WTString MultiParse::s_SrcNamespaceHints;
static CSpinCriticalSection s_NamespaceLock;
MultiParse::NsMap MultiParse::s_scopedNamespaceMap;

static phmap::flat_hash_map<WTString, std::pair<WTString, uint32_t>> s_scopedNamespaceCache;
static uint32_t s_scopedNamespaceCache_lastused = 0;


namespace std
{
template <typename K, typename V, typename Hash, typename Eq, typename Alloc, typename Pr>
size_t erase_if(phmap::flat_hash_map<K, V, Hash, Eq, Alloc>& cont, Pr pred)
{
	const auto old_size = cont.size();
	for(auto it = cont.begin(), end = cont.end(); it != end;)
	{
		if (pred(*it))
			it = cont.erase(it);
		else
			++it;
	}
	return old_size - cont.size();
}
}

WTString MultiParse::GetNameSpaceString(WTString scopeContext) const
{
	if (!IsCFile(m_ftype))
		return WTString();

	AutoLockCs l(s_NamespaceLock);
	if (auto it = s_scopedNamespaceCache.find(scopeContext); it != s_scopedNamespaceCache.end())
	{
		auto& [iterUsings, lastused] = it->second;
		if (!iterUsings.IsEmpty())
			vLog("   GNSS: returning ns cache for scope %s (%s)\n", scopeContext.c_str(), iterUsings.c_str());

		lastused = ++s_scopedNamespaceCache_lastused;
		return iterUsings;
	}

	WTString lst = DoGetNameSpaceString(scopeContext);
	if (!lst.IsEmpty())
		vLog("   GNSS: built ns cache for scope %s (%s)\n", scopeContext.c_str(), lst.c_str());
	s_scopedNamespaceCache[scopeContext] = std::make_pair(lst, ++s_scopedNamespaceCache_lastused);

	constexpr int kTrimCacheSize = 100;
	constexpr int kMaxCacheSize = kTrimCacheSize * 2;
	if (s_scopedNamespaceCache.size() >= kMaxCacheSize)
	{
		std::erase_if(s_scopedNamespaceCache, [](const auto& kv) {
			auto& [iterUsings, lastused] = kv.second;
			bool ret = lastused <= (s_scopedNamespaceCache_lastused - kTrimCacheSize);
			if (ret)
				vLog("   GNSS: popping ns cache for scope %s\n", kv.first.c_str());
			return ret;
		});
	}
	return lst;
}

WTString MultiParse::DoGetNameSpaceString(WTString scopeContext) const
{
	WTString lst;

	const WTString terminatedScopeContext = std::move(scopeContext) + DB_SEP_STR;

	std::vector<WTString> nsList;
	nsList.reserve(1024);

	for (auto mapIter = s_scopedNamespaceMap.begin(); mapIter != s_scopedNamespaceMap.end(); ++mapIter)
	{
		const WTString& iterScope = mapIter->first;
// 		WTString terminatedIterScope;
// 		terminatedIterScope.AssignCopy(iterScope);
// 		terminatedIterScope += DB_SEP_STR;

//		if (terminatedScopeContext.find(terminatedIterScope) == 0)
		if(terminatedScopeContext.begins_with2(iterScope, DB_SEP_CHR))
		{
			assert(mapIter->second);
			const NsSet& iterSet = *mapIter->second;
			nsList.insert_range(nsList.end(), iterSet);
		}
	}

	std::sort(nsList.begin(), nsList.end());
// 	nsList.sort();
//	nsList.unique();
	WTString last = "??";
	for (const auto &ns : nsList)
	{
		if (last == ns)
			continue;
		last = ns;

		lst += ns;
		lst += "\f";
// 		vLog("   GNSS:     adding ns %s\n", ns);
	}

	return lst;
}

void MultiParse::AddNamespaceString(WTString scope, WTString ns)
{
	_ASSERTE(IsCFile(m_ftype));

	WTString searchScope = scope;
	searchScope.ReplaceAll(".", DB_SEP_STR);

	WTString targetSym = ns;
	targetSym.ReplaceAll(".", DB_SEP_STR);

	// namespace resolution could fail if we are being called from LoadIdxesInDir
	// because the FindExact2 call in the loop would be dependent upon the order
	// of loads of idx files.
	// #namespaceResolutionOrderDependency
	DType* resolvedNs = NULL;
	for (;;)
	{
		WTString searchSym = searchScope + targetSym;
		resolvedNs = this->FindExact2(searchSym);
		if (resolvedNs)
			break;

		// break if just searched global scope
		if (searchScope.GetLength() == 0)
			break;

		searchScope = ::StrGetSymScope(searchScope);
	}

	if (resolvedNs)
	{
		auto usedNs = resolvedNs->SymScope();

		if (scope.find(usedNs + DB_SEP_STR) == 0 || scope == usedNs)
		{
			vCatLog("Parser.MultiParse", "  ANSS: %s uses %s (resolved from %s) [REDUNDANT]\n", scope.c_str(), usedNs.c_str(), ns.c_str());
		}
		else
		{
			AutoLockCs l(s_NamespaceLock);

			NsSetPtr& scopeSetPtr = s_scopedNamespaceMap[scope];
			if(!scopeSetPtr)
				scopeSetPtr = std::make_unique<NsSet>();
			NsSet& scopeSet = *scopeSetPtr;
			auto [_, inserted] = scopeSet.insert(usedNs);
			if (inserted)
			{
				vCatLog("Parser.MultiParse", "  ANSS: %s uses %s (resolved from %s) [FLUSHES CACHE]\n", scope.c_str(), usedNs.c_str(),
				     ns.c_str());

				s_scopedNamespaceCache.clear();

				// Add anything the used NS uses to this scope
				auto targetIter = s_scopedNamespaceMap.find(usedNs);
				if (targetIter != s_scopedNamespaceMap.end())
					scopeSet.insert(targetIter->second->begin(), targetIter->second->end());

				// Add the used NS to any scope that uses this scope
				for (auto& [iterScope, iterSetPtr] : s_scopedNamespaceMap)
				{
					if (iterScope != usedNs)
						iterSetPtr->insert(usedNs);
				}
			}
			else
			{
// 				vCatLog("Parser.MultiParse", "  ANSS: %s uses %s (resolved from %s) [ALREADY ADDED]\n", scope, usedNs, ns);
			}
		}
	}
	else
	{
		vCatLog("Parser.MultiParse", "  ANSS: %s uses %s [UNRESOLVED]\n", scope.c_str(), ns.c_str());
	}
}

WTString MultiParse::GetGlobalNameSpaceString(bool includeHints /*= true*/)
{
	int typeToCompare = FileType();
	if (Is_Tag_Based(typeToCompare))
		typeToCompare = HTML;

	switch (typeToCompare)
	{
	case Src:
	case Header:
	case UC: {
		AutoLockCs l(s_NamespaceLock);
		if (GlobalProject && GlobalProject->CppUsesClr())
		{
			const int pos = s_SrcNamespaces.Find(":cli\f");
			if (0 != pos)
			{
				if (-1 != pos)
				{
					// [case: 87571] clear it; we'll move it up front
					s_SrcNamespaces.ReplaceAll("\f:cli\f", "\f");
				}

				// [case: 23892] When compiling with /clr, "using namespace cli" is implied.
				// https://msdn.microsoft.com/en-us/library/ts4c4dw6.aspx
				s_SrcNamespaces.prepend(":cli\f");
			}
		}

		WTString ret(s_SrcNamespaces);
		if (includeHints)
		{
			// [case: 98073]
			_ASSERTE(s_SrcNamespaceHints.IsEmpty() || s_SrcNamespaceHints[s_SrcNamespaceHints.GetLength() - 1] == '\f');
			ret += s_SrcNamespaceHints;
		}

		if (!m_namespaces.IsEmpty())
		{
			// [case: 142375]
			ret = m_namespaces + ret;
		}
		return ret;
	}
	case VB:
	case VBS:
		if (!m_namespaces.GetLength())
		{
			m_namespaces = ":Microsoft:VisualBasic\f"
			               ":Microsoft:VisualBasic:Constants\f"
			               ":Microsoft:VisualBasic:Conversion\f"
			               ":Microsoft:VisualBasic:DateAndTime\f"
			               ":Microsoft:VisualBasic:FileSystem\f"
			               ":Microsoft:VisualBasic:Financial\f"
			               ":Microsoft:VisualBasic:Globals\f"
			               ":Microsoft:VisualBasic:Interaction\f"
			               ":Microsoft:VisualBasic:Strings\f"
			               ":Microsoft:VisualBasic:VBMath\f"

			               ":System\f"
			               ":System:Collections\f"
			               ":System:Collections:Generic\f"
			               ":System:Data\f"
			               ":System:Diagnostic\f";
		}
		return m_namespaces;
	case HTML:
	case JS:
	case PHP:
		if (!m_namespaces.GetLength())
		{
			m_namespaces = ":\f"
			               ":Window\f"
			               ":Document\f"
			               ":Guess\f";
		}
		return m_namespaces;

	case CS:
	default:
		return m_namespaces;
	}
}

void MultiParse::SetGlobalNameSpaceString(const WTString& namespaces)
{
	int typeToCompare = FileType();
	if (Is_Tag_Based(typeToCompare))
		typeToCompare = HTML;

	switch (typeToCompare)
	{
	case Src:
	case Header:
	case UC: {
		AutoLockCs l(s_NamespaceLock);
		s_SrcNamespaces = namespaces;
	}
	break;
	case VB:
	case VBS:
	case HTML:
	case JS:
	case PHP:
	case CS:
	default:
		m_namespaces = namespaces;
		break;
	}
}

void MultiParse::AddGlobalNamespaceHint(const WTString& namespaces)
{
	// [case: 98073]
	AutoLockCs l(s_NamespaceLock);
	if (s_SrcNamespaceHints.GetLength() > 512)
	{
		// chomp old hints
		int pos = s_SrcNamespaceHints.Find('\f', 256);
		if (-1 != pos)
		{
			s_SrcNamespaceHints.LeftInPlace(pos + 1);
			_ASSERTE(s_SrcNamespaceHints[s_SrcNamespaceHints.GetLength() - 1] == '\f');
		}
	}

	_ASSERTE(!namespaces.IsEmpty() && namespaces[namespaces.GetLength() - 1] == '\f');
	s_SrcNamespaceHints.prepend(namespaces);
}

static constexpr char g_encodeChar = 0xe;
static constexpr char g_encodeChars[] = "<> .\t\r\n-=,&*^\"'()!";
static constexpr int kEncodeCharsLen = sizeof(g_encodeChars) - 1;

class encodeCharsTable
{
  public:
	constexpr encodeCharsTable()
	{
		for (int i = 0; i < 256; i++)
			decode_chars[i] = encode_chars[i] = (char)i;
		for (int i = 0; i < kEncodeCharsLen; i++)
		{
			encode_chars[g_encodeChars[i]] = char(g_encodeChar + i);
			decode_chars[g_encodeChar + i] = g_encodeChars[i];
		}
#ifdef _DEBUG
		initialized = true;
#endif
	}

	constexpr char get_encode_char(char c) const
	{
#ifdef _DEBUG
		assert(initialized);
#endif
		return encode_chars[(uint8_t)c];
	}
	constexpr char get_decode_char(char c) const
	{
#ifdef _DEBUG
		assert(initialized);
#endif
		return decode_chars[(uint8_t)c];
	}

  protected:
#ifdef _DEBUG
	bool initialized = false;
#endif
	char encode_chars[256] = {};
	char decode_chars[256] = {};
};
static constinit encodeCharsTable ect;

// frequently used routines; original is quite slow using strchr for every char
// new routine uses table lookup; left assert to compare orig version to the new in debug builds, but it seems to be working fine
#ifdef _DEBUG
static char EncodeCharOrig(TCHAR c)
{
	LPCSTR p = strchr(g_encodeChars, c);
	return (c && p) ? (TCHAR)(g_encodeChar + (p - g_encodeChars)) : c;
}
#endif
char EncodeChar(TCHAR c)
{
	assert(ect.get_encode_char(c) == EncodeCharOrig(c));
	return ect.get_encode_char(c);
}

#ifdef _DEBUG
static char DecodeCharOrig(TCHAR c)
{
	int en_offset = c - g_encodeChar;
	if (en_offset >= 0 && en_offset < kEncodeCharsLen)
	{
		return g_encodeChars[en_offset];
	}
	return c;
}
#endif
char DecodeChar(TCHAR c)
{
	assert(ect.get_decode_char(c) == DecodeCharOrig(c));
	return ect.get_decode_char(c);
}

WCHAR DecodeCharW(WCHAR c)
{
	int en_offset = c - g_encodeChar;
	if (en_offset >= 0 && en_offset < kEncodeCharsLen)
	{
		return (WCHAR)g_encodeChars[en_offset];
	}
	return c;
}

#ifdef _DEBUG
static WTString EncodeScopeOrig(LPCSTR scope)
{
	// encode scope so it can contain spaces and other symbols
	WTString s = scope;
	for (int l = s.GetLength(), i = 0; i < l; i++)
	{
		LPCSTR p = strchr(g_encodeChars, s[i]);
		if (p)
		{
			s.SetAt(i, (TCHAR)(g_encodeChar + (p - g_encodeChars)));
		}
	}
	return s;
}
#endif
WTString EncodeScope(LPCSTR scope)
{
	WTString s(' ', scope ? strlen_i(scope) : 0);
	if (s.IsEmpty())
		return std::move(s);
	std::transform(scope, scope + strlen(scope), s.data(), EncodeChar);
	assert(s == EncodeScopeOrig(scope));
	return s;
}
void EncodeScopeInPlace(WTString& scope)
{
	if (scope.IsEmpty())
		return;
	scope.CopyBeforeWrite();
	std::transform(scope.data(), scope.data() + scope.length(), scope.data(), EncodeChar);
}

#ifdef _DEBUG
static WTString DecodeScopeOrig(LPCSTR scope)
{
	WTString s = scope;
	for (int l = s.GetLength(), i = 0; i < l; i++)
	{
		int en_offset = s[i] - g_encodeChar;
		if (en_offset >= 0 && en_offset < kEncodeCharsLen)
		{
			s.SetAt(i, g_encodeChars[en_offset]);
		}
	}
	return s;
}
#endif
WTString DecodeScope(LPCSTR scope)
{
	WTString s(' ', scope ? strlen_i(scope) : 0);
	if (s.IsEmpty())
		return std::move(s);
	std::transform(scope, scope + strlen(scope), s.data(), DecodeChar);
	assert(s == DecodeScopeOrig(scope));
	return s;
}
void DecodeScopeInPlace(WTString& scope)
{
	if(scope.IsEmpty())
		return;
	scope.CopyBeforeWrite();
	std::transform(scope.data(), scope.data() + scope.length(), scope.data(), DecodeChar);
}

CStringW DecodeScope(const CStringW& scope)
{
	CStringW s = scope;
	for (int l = s.GetLength(), i = 0; i < l; i++)
	{
		int en_offset = s[i] - g_encodeChar;
		if (en_offset >= 0 && en_offset < kEncodeCharsLen)
		{
			s.SetAt(i, (WCHAR)g_encodeChars[en_offset]);
		}
	}
	return s;
}

// m_passtype flag
// static LPCSTR ParseTo(LPCSTR code, char c)
//{
//	int inparen = 0;
//	char instr = '\0';
//	for(; *code; code++)
//	{
//		if(instr)
//		{
//			if(*code == instr)
//				instr = '\0';
//			if(*code == '\\' && code[1])
//				code++;
//			continue;
//		}
//		if(!inparen && *code == c)
//			return code;
//		if(strchr("({[", *code))
//			inparen++;
//		else if(strchr(")}]", *code))
//		{
//			if(!inparen)
//				return code;
//			inparen--;
//		}
//		else if(strchr("'\"", *code))
//			instr = *code;
//		else if(*code == '/')
//		{
//			if(code[1] == '/')
//			{
//				for(; *code && *code != '\r' && *code != '\n'; code++);
//				for(; *code && (code[1] == '\r' || code[1] == '\n'); code++); // leave \n in there to get eaten on
// code++
//			}
//			if(code[1] == '*')
//			{
//				LPCSTR p = strstr(&code[2], "*/");
//				if(p)
//					code = p + 1; // code will get incremented to eat trailing '/'
//				// else unmatched comment
//			}
//		}
//
//	}
//	return nullptr;
//}

#ifdef _DEBUG
CCriticalSection gMpsLock;
std::set<MultiParse*> gMps;
#endif

MultiParse::MultiParse(int ftype /*= 0*/, int hashRows /*= -1*/)
    : ScopeInfo(ftype), m_DefBuf(MAXLN + 10)
{
#ifdef _DEBUG
	{
		AutoLockCs l(gMpsLock);
		gMps.insert(this);
		// 		CString msg;
		// 		msg.Format("mp+: %p\n", this);
		// 		OutputDebugStringA(msg);
	}
#endif
	LOG("MParse ctor 1");
	_ASSERTE(ftype >= 0);
	if (!ftype)
		m_ftype = gTypingDevLang;
	if (hashRows > 0)
	{
		m_pLocalDic = std::make_shared<Dic>((uint)hashRows);
		m_pLocalHcbDic = std::make_shared<FileDic>("", false, (uint)hashRows);
	}
	else
	{
		m_pLocalDic = std::make_shared<Dic>(10000u);
		m_pLocalHcbDic = std::make_shared<FileDic>("", false, 10000u);
	}
	Init();
}

MultiParse::MultiParse(MultiParsePtr mp, int ftype, int hashRows /*= -1*/)
    : ScopeInfo(ftype), m_mp(mp), m_DefBuf(MAXLN + 10)
{
#ifdef _DEBUG
	{
		AutoLockCs l(gMpsLock);
		gMps.insert(this);
		// 		CString msg;
		// 		msg.Format("mp+: %p\n", this);
		// 		OutputDebugStringA(msg);
	}
#endif
	LOG("MParse ctor 2");
	_ASSERTE(ftype >= 0);
	if (!ftype)
		m_ftype = gTypingDevLang;
	if (mp)
	{
		m_pLocalDic = mp->LDictionary();
		m_pLocalHcbDic = mp->LocalHcbDictionary();
	}
	else if (hashRows > 0)
	{
		m_pLocalDic = std::make_shared<Dic>((uint)hashRows);
		m_pLocalHcbDic = std::make_shared<FileDic>("", false, (uint)hashRows);
	}
	else
	{
		m_pLocalDic = std::make_shared<Dic>(10000u);
		m_pLocalHcbDic = std::make_shared<FileDic>("", false, 10000u);
	}
	Init();
}

MultiParse::~MultiParse()
{
#ifndef VA_CPPUNIT
#ifdef _DEBUG
	{
		AutoLockCs l(gMpsLock);
		gMps.erase(this);
		// 		CString msg;
		// 		msg.Format("mp-: %p\n", this);
		// 		OutputDebugStringA(msg);
	}
#endif
#endif
	LOG2("MParse dtor");
	_ASSERTE(!mPendingIncludes.size());
}

void MultiParse::ResetDefaultNamespaces()
{
	AutoLockCs l(s_NamespaceLock);
	s_SrcNamespaces = kDefaultNamespaces;
	s_SrcNamespaceHints.Empty();

	s_scopedNamespaceMap.clear();
	s_scopedNamespaceCache.clear();
}

// Misc/*.va files no longer added to global dict, each lang has it's own file.
static CSpinCriticalSection sLangMpCacheLock;
static MultiParsePtr gLanguageMpCache[kLanguageFiletypeCount];
volatile bool sMiscFilesLoaded[kLanguageFiletypeCount];

void FreeDFileMPs()
{
	// clear cache
	AutoLockCs l(sLangMpCacheLock);
	for (int idx = 0; idx < kLanguageFiletypeCount; ++idx)
		gLanguageMpCache[idx] = nullptr;

	ZeroMemory((void*)sMiscFilesLoaded, sizeof(sMiscFilesLoaded));
}

void ReloadDFileMP(int ftype)
{
	_ASSERTE(ftype < kLanguageFiletypeCount);
	AutoLockCs l(sLangMpCacheLock);
	if (!ftype && gTypingDevLang)
		ftype = gTypingDevLang;
	if (-1 == ftype || !ftype)
		ftype = Src;
	if (ftype == Header || ftype == UC)
		ftype = Src;

	MultiParsePtr oldMp = gLanguageMpCache[ftype];
	if (!oldMp)
		return;

	gLanguageMpCache[ftype] = nullptr;
	GetDFileMP(ftype);
}

MultiParsePtr GetDFileMP(int ftype)
{
	_ASSERTE(ftype < kLanguageFiletypeCount);
	if (!ftype && gTypingDevLang)
		ftype = gTypingDevLang;
	if (-1 == ftype || !ftype)
		ftype = Src;
	if (ftype == Header || ftype == UC)
		ftype = Src;
	if (!gLanguageMpCache[ftype])
	{
		AutoLockCs l(sLangMpCacheLock);
		if (!gLanguageMpCache[ftype])
		{
			CStringW dfile;
			if (ftype == Src)
				dfile = VaDirs::GetParserSeedFilepath(L"CPP.VA");
			else if (ftype == CS)
				dfile = VaDirs::GetParserSeedFilepath(L"CS.VA");
			else if (Is_VB_VBS_File(ftype))
				dfile = VaDirs::GetParserSeedFilepath(L"VB.VA");
			else if (ftype == JS)
				dfile = VaDirs::GetParserSeedFilepath(L"Java.VA");
			else if (ftype == XML || ftype == XAML)
				;
			else if (Is_Tag_Based(ftype))
				dfile = VaDirs::GetParserSeedFilepath(L"HTML.VA");
			else if (ftype == Idl)
				dfile = VaDirs::GetParserSeedFilepath(L"IDL.VA");
			else if (ftype == RC)
				dfile = VaDirs::GetParserSeedFilepath(L"RC.VA");
			else
				dfile = VaDirs::GetParserSeedFilepath(L"HTML.VA");

			if (Src == ftype || CS == ftype)
			{
				if (dfile.IsEmpty() || !IsFile(dfile))
				{
					_ASSERTE(!"GetDFileMP missing .va file");
					vLog("ERROR: GetDFileMP missing .va file");
				}
			}

			MultiParsePtr newMp = MultiParse::Create(ftype, dfile.IsEmpty() ? 1 : 250);
			if (dfile.GetLength())
				newMp->ReadDFile(dfile, V_LOCAL, 0, OLD_DB_FIELD_DELIMITER);
			newMp->mIsVaReservedWordFile = TRUE;

			if (ftype == Src && Psettings->mUnrealScriptSupport)
			{
				// Include both CPP.va and UC.va files
				dfile = VaDirs::GetParserSeedFilepath(L"UC.VA");
				newMp->ReadDFile(dfile, V_LOCAL);
			}

			if (ftype == Src && Psettings->mUnrealEngineCppSupport)
			{
				// Include both CPP.va and uecpp.va files
				dfile = VaDirs::GetParserSeedFilepath(L"uecpp.VA");
				newMp->ReadDFile(dfile, V_LOCAL);
			}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
			if (ftype == Src)
			{
				const char* const resWords[] = {":__closure", ":__classmethod", ":__published", ":__property"};
				for (const auto cur : resWords)
					newMp->AddResword(WTString(cur));
			}
#endif
			if (!dfile.IsEmpty() && !newMp->LDictionary()->GetHashTable()->GetItemsInContainer())
			{
				// coloring on UI thread while project closing/reloading
				ftype = Other;
				if (gLanguageMpCache[ftype])
				{
					// toss the one we just created, and return the cached one
					return gLanguageMpCache[ftype];
				}
			}
			else
			{
				_ASSERTE(newMp->FileType() == ftype); // $$MSD
			}

			gLanguageMpCache[ftype] = newMp;
		}
	}

	return gLanguageMpCache[ftype];
}

DType* MultiParse::FindSym(const WTString* sym, const WTString* scope, const WTString* baseclasslist,
                           int findFlag /*= FDF_NONE*/)
{
	DB_READ_LOCK;
	DEFTIMER(FindSymTimer);
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		WTString bcl;
		WTString ns;
		if (scope && !(findFlag & FDF_IgnoreGlobalNamespace))
			ns = GetGlobalNameSpaceString();
		if (baseclasslist && *(*baseclasslist).c_str())
			bcl = *baseclasslist + '\f';
		FindData fds(sym, scope, &bcl, &ns, findFlag);
		DType* resdata = GetDFileMP(FileType())->m_pLocalDic->Find(&fds);
		// pick out reserved words
		if (resdata)
		{
			switch (resdata->MaskedType())
			{
			case CLASS: // added for local typedefs
				if (!resdata->Def().contains("typedef"))
					break;
				// allow user to override these types, see bellow
				//			case TYPE:
			case RESWORD:
				return resdata;
			}
		}
		// Added the code below to fix Invalid underlining in explicit overriding
		// http://www.wholetomato.com/forum/topic.asp?TOPIC_ID=4014
		if (!(findFlag & FDF_CONSTRUCTOR) && !scope && sym && fds.scopeArray.Contains(WTHashKey(DB_SEP_STR + *sym)))
			return FindExact2(DB_SEP_STR + *sym);
		DType* data = NULL;
		if (!data && (FileType() == Src || FileType() == UC) &&
		    !(findFlag & FDF_CONSTRUCTOR))     // [case: 22641] exclude ctor searches
			data = m_pLocalHcbDic->Find(&fds); // [case: 21352] better support for classes defined in src files
		if (!data)
			data = m_pLocalDic->Find(&fds);
		if (!data)
			data = g_pGlobDic->Find(&fds);
		if (!data)
			data = SDictionary()->Find(&fds);
		if (!data && scope && !(findFlag & FDF_NoNamespaceGuess))
		{
			// shouldn't this check for findFlag & FDF_GUESS? (added FDF_NoNamespaceGuess because of this lingering
			// question)
			DType* anyData = FindAnySym(*sym);
			if (anyData)
			{
				DType* namespaceData = FindExact(anyData->Scope());
				if (!namespaceData && anyData->IsDbLocal() && (Src == FileType() || Header == FileType()))
				{
					if (anyData->IsImpl() && anyData->IsConstructor() && anyData->Sym() == StrGetSym(anyData->Scope()))
					{
						// [case: 94608]
						// anyData is the ctor impl in local file; but not explicitly scoped.
						// search project db for the fully scoped class
						DType* tmp = g_pGlobDic->FindAnySym(*sym);
						if (tmp)
						{
							namespaceData = FindExact(tmp->Scope());
							if (namespaceData && namespaceData->type() == NAMESPACE)
								anyData = tmp;
						}
					}
					else if (anyData->HasLocalScope() && anyData->type() == VAR)
					{
						// [case: 7204]
						// anyData is the local function parameter and is missing the namespace scope
						const WTString anyDataSymScope(anyData->SymScope());
						WTString prospectiveSymScope(anyDataSymScope);
						::ScopeToSymbolScope(prospectiveSymScope);
						prospectiveSymScope = ::StrGetSymScope(prospectiveSymScope);
						const uint fid = anyData->FileId();
						WTString fileScopedUsing;
						fileScopedUsing.WTFormat(":wtUsingNamespace_%x", fid);
						DTypeList dtList;
						FindExactList(fileScopedUsing, dtList, false);
						for (auto& d : dtList)
						{
							if (d.FileId() == fid && d.MaskedType() == RESWORD && d.Attributes() & V_IDX_FLAG_USING)
							{
								const WTString ns2(d.Def());
								DType* tmp = FindExact(ns2 + prospectiveSymScope);
								if (tmp && tmp->type() == FUNC)
								{
									const WTString fullSymScope(*scope + DB_SEP_STR + *sym);
									if (fullSymScope.Find(anyDataSymScope) > 0 &&
									    fullSymScope.Find(tmp->SymScope()) == 0)
									{
										data = anyData;
										break;
									}
								}
							}
						}
					}
				}

				if (namespaceData)
				{
					if (namespaceData->type() == NAMESPACE)
					{
						// This symbol is from another namespace.

						if (!(findFlag & FDF_NoAddNamespaceGuess))
						{
							// [case: 18881] we won't suggest using statements if we already
							// have the namespace in our namespace string
							if (Src == FileType() || Header == FileType() || UC == FileType())
							{
								const WTString nsToAdd(namespaceData->SymScope() + "\f");
								WTString globalNamespaces = GetGlobalNameSpaceString();
								if (-1 == globalNamespaces.Find("\f" + nsToAdd))
								{
									// Assume one of the headers this file includes has a "using " for this namespace.
									// This fixes some boost lib problems (p4 change 5309)
									// [case: 58701] fixes Qt since VA parses all Qt as being in QT_NAMESPACE
									// [case: 98073]
									AddGlobalNamespaceHint(nsToAdd);
									vCatLog("Parser.MultiParse", "MP::FS addGns %s for %s %s\n", nsToAdd.c_str(), scope ? scope->c_str() : "",
									     sym ? sym->c_str() : "");
								}
							}
						}

						data = anyData;
					}
#ifdef ALTERNATE_SCOPE_CHECK
					// this was a hack for case 109768 that is a bit of a mirror to the above
					// NAMESPACE block; leaving until confident the ScopeHashAry change will stick
					else if (CLASS == namespaceData->type() || STRUCT == namespaceData->type())
					{
						// [case: 109768]
						WTString adScp(anyData->Scope());
						if (-1 != bcl.Find("\f" + adScp + DB_SEP_STR))
							data = anyData;
					}
#endif
				}
			}
		}

		if (!data && resdata && resdata->MaskedType() == TYPE)
			return resdata;

		return data;
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("MP:");
		_ASSERTE(!"exception caught in MultiParse::FindSym");
		Log("Exception caught in FindSym\n");
		// just make sym undefined
		return NULL;
	}
#endif // !SEAN
}

void MultiParse::CwScopeInfo(WTString& scope, WTString& baseclasslist, int devLang)
{
	DB_READ_LOCK;
	if (m_xref)
	{
		LogElapsedTime let("MP::CWSI(1)", scope, 50);

		WTString bscope = m_xrefScope;
		bscope.ReplaceAll('.', ':');
		DType dat(GetCwData().get());
		if (!dat.IsDbLocal() && -1 != dat.Def().Find('\f'))
		{
			// [case: 67831]
			// concat'd defs on xref can be indicative of duplicate symbols
			dat = FindBetterDtype(&dat);
			mUpdatedXref = std::make_shared<DType>(dat);
		}
		WTString bcl = GetBaseClassList(&dat, bscope, false, 0, devLang) + DB_SEP_STR;
		mUpdatedXref.reset();

		if (m_xrefScope == DB_SEP_STR)
		{ // case "::method"
			scope = DB_SEP_STR;
			baseclasslist = NULLSTR;
		}
		else
		{
			scope = NULLSTR;
			baseclasslist = bcl;
		}
	}
	else
	{
		LogElapsedTime let("MP::CWSI(2)", scope, 50);

		scope = m_lastScope;
		if (scope.length() && !m_isMethodDefinition)
		{
			baseclasslist = m_baseClassList;
			baseclasslist += GetNameSpaceString(scope);
			baseclasslist += GetGlobalNameSpaceString();
		}
		else
			baseclasslist = m_baseClassList;
	}

	if (g_loggingEnabled)
	{
		vLog("CwScopeInfo scp(%s) xr(%d) bclen(%d) xrslen(%d)\n", scope.c_str(), m_xref, baseclasslist.GetLength(),
		     m_xrefScope.GetLength());
		if (m_xref)
			vLog("CSI xrs(%s)\n", m_xrefScope.c_str());
		WTString bcl(baseclasslist);
		bcl.ReplaceAll("\f", ";");
		vLog("CSI bcl(%s)\n", bcl.c_str());
		if (baseclasslist.GetLength() > 999)
			vLog(" (end bcl)\n" /*, baseclasslist.c_str()*/);
	}
}

/*
void ReadUserDatFile(LPCSTR file, int colorType)
{
    WTString dat, def;
    dat.ReadFile(file);
    LPCSTR p1 = dat;
    WTString sym;
    char buf[130];
    strcpy(buf, ":_VA_UserData:");
    g_pGlobDic->add(WTString(":_VA_UserData:") + "user_dat", def, colorType);
    int len = strlen(buf);
    while(*p1){
        int i;
        for(i = 0;ISALNUM(p1[i]) && i < 100; i++){
            buf[len + i] = p1[i];
        }
        buf[len + i] = '\0';
        for(;p1[i] && !ISALNUM(p1[i]); i++);
        p1 += i;
        sym.assign(buf);
        g_pGlobDic->add(sym, def, colorType);
    }
}
*/

void MultiParse::CwScope(WTString& scope, WTString& sym, WTString& def, uint& type, uint& attrs, uint& dbFlags)
{
	DB_READ_LOCK;

	LogElapsedTime let("MP::CWS", scope, 50);

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		BOOL typing = g_currentEdCnt ? g_currentEdCnt->m_typing : FALSE;
		if ((m_type || mTypeAttrs) && m_type != PREPROCSTRING) // in comment, WTString or # directive...
		{
			type = m_type;
			attrs = mTypeAttrs;
			dbFlags = 0;
			return;
		}
		if (wt_isdigit(sym[0]))
		{
			type = NUMBER;
			attrs = V_INFILE;
			return;
		}
		if (wt_isspace(sym[0]))
		{
			type = CTEXT;
			return;
		}
		if (sym.length() && !ISALPHA(sym[0]))
		{
			if ((sym[0] == '"') || (sym[0] == '\''))
			{
				type = STRING;
				return;
			}
			if (sym[0] == '/' && (sym[1] == '/' || sym[1] == '*'))
			{
				type = COMMENT;
				return;
			}
			if (sym[0] == '#')
			{
				type = DEFINE;
				return;
			}

			type = SYM;
			attrs = V_INFILE;
			return;
		}

		WTString context, definition, bcl;
		DType* data = NULL;
		// change all foo.bar's to foo:bar
		token2 t = m_xrefScope;
		t.ReplaceAll(".", ":");
		t.ReplaceAll("::", ":");
		m_xrefScope = t.c_str();

		if (m_xref)
		{
			if (/*Psettings->m_strictParse &&*/ m_xrefScope.GetLength() == 0)
				return;
			bcl = m_xrefScope + "\f" + GetBaseClassList(m_xrefScope) + DB_SEP_STR;
			if (sLastBcl1() != bcl)
			{
				// refresh bcl in case a type was defined later
				sLastBcl1() = bcl;
				if (bcl.contains(WILD_CARD_SCOPE))
					bcl = GetBaseClassList(m_xrefScope, true) + DB_SEP_STR;
			}

			if (sym == "operator")
			{
				type = RESWORD;
				return;
			}

			if (!m_xrefScope.length() || m_xrefScope == DB_SEP_STR)
				data = FindSym(&sym, &bcl, NULL);
			else
			{
				data = FindSym(&sym, NULL, &bcl);
				if (!data)
					data = FindSym(&sym, NULL, &bcl, FDF_CONSTRUCTOR);
			}
			///////////////////////
			// there is a case in which a baseclass changes
			// and all vars of type x that inherit that baseclass
			// don't see the mod and need updating of their baseclasslist.
			if (!data)
			{
				/// so we don't harp on the same xref over and over
				if (m_xrefScope != sLastXref())
				{
					// force rehash of bcl
					GetBaseClassList(m_xrefScope, true);
					sLastXref() = m_xrefScope;
				}
			}
		}
		else
		{
			WTString lscope = m_lastScope;
			// change all '.'s to ':'
			for (int i = lscope.length(); i; i--)
			{
				if (lscope[i - 1] == '.')
					lscope.SetAt(i - 1, ':');
			}

			m_baseClassList = GetBaseClassList(m_baseClass);
			if (sLastBcl2() != m_baseClassList)
			{
				// refresh bcl in case a type was defined later
				sLastBcl2() = m_baseClassList;
				if (m_baseClassList.contains(WILD_CARD_SCOPE))
					m_baseClassList = GetBaseClassList(m_baseClass, true) + DB_SEP_STR;
			}

			bcl = m_baseClassList;
			data = FindSym(&sym, &lscope, &m_baseClassList);
			///////////////////////
			// there is a case in which a baseclass changes
			// and all vars of type x that inherit that baseclass
			// don't see the mod and need updating of their baseclasslist.
			// look to see if the user is typing so Suggestions work.
			if (!data && !typing)
			{
				/// so we don't harp on the same xref over and over
				if (lscope != sLastScope())
				{
					// get class name from scope
					token2 t2 = lscope;
					WTString bscope = t2.read(':');
					int p = bscope.ReverseFind('.');
					if (p != NPOS)
					{
						bscope.MidInPlace(0, p);
						bscope.ReplaceAll('.', ':');
					}
					m_baseClassList = GetBaseClassList(bscope) + DB_SEP_STR;
					sLastScope() = lscope;
				}
			}
		}

		if (data)
		{
			def = data->Def();
			scope = data->SymScope();
			vLog("CwScope %s 0x%x %x %x %s", scope.c_str(), data->MaskedType(), data->Attributes(), data->DbFlags(),
			     def.c_str());
			// inherits hack...
			// setting type to RESWORD blanks def and context, and messes up inheritance.
			// We only want to remove inherits from def and leave others
			if (def.contains("inherits"))
			{
				token t2 = def;
				t2.ReplaceAll(TRegexp("inherits .*\f"), string(""));
				t2.ReplaceAll(TRegexp("inherits .*"), string(""));
				def = t2.c_str();
			}

			type = data->MaskedType();
			attrs = data->Attributes();
			dbFlags = data->DbFlags();
			return;
		}

		if (m_scopeType == ASM)
		{
			type = ASM;
			return; // no red or auto suggest in asm
		}
		//		if(m_xref && bcl.contains(WILD_CARD_SCOPE))	// make T.UnknownMember black if in template
		//			return CTEXT;
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("MP:");
		ASSERT(FALSE);
		Log("Exception caught in CwScope\n");
	}
#endif // !SEAN
	return;
}

void MultiParse::Init()
{
	ClearCwData();
	mUpdatedXref.reset();
	m_inParenCount = 0;
	m_scopeType = 0;
	m_argParenOffset = 0;
	m_argTemplate.Empty();
	m_argScope.Empty();
	m_p = NULL;
	m_cp = m_len = m_parseAll = 0;
	m_line = m_ed ? (m_ed->LineFromChar(0)) : 1;
	m_isDef = false;
	m_firstWord.Empty();
	m_baseClassList.Empty();
	m_baseClass.Empty();
	m_lastScope.Empty();
	m_type = 0;
	mTypeAttrs = 0;
	m_xref = false;
	m_stop = false;
	m_spellFromPos = -1;
	m_isMethodDefinition = FALSE;
	mDbOutMacroLocalCnt = mDbOutMacroSysCnt = mDbOutMacroProjCnt = 0;
}

WTString MultiParse::SubStr(int p1, int p2)
{
	DEFTIMER(SubStrTimer);

	int i = 0;
	ASSERT(p1 <= m_len);
	for (; (i + p1) < p2 && i < MAXLN && m_p[p1 + i]; i++)
		;

	if (!i)
		return WTString();

	WTString retval(&m_p[p1], i);
	if (i == MAXLN)
		retval += "...";
	return retval;
}

void MultiParse::ParseFileForGoToDef(const CStringW& file, bool sysFile, LPCSTR text /*= NULL*/,
                                     int extraDbFlags /*= 0*/)
{
	DEFTIMERNOTE(ParseFileForGoToDefTimer, CString(file));
	ASSERT_LOOP(40);
	const CStringW srcfile = MSPath(file);
	if (!IsFile(file))
		return;
	if (IsIncluded(file, FALSE))
		return;
	if (GetFileType(srcfile) != Src)
	{
#if !defined(NDEBUG)
		CStringW ext(GetBaseNameExt(srcfile));
		if (ext.CompareNoCase(L"inl"))
		{
			ASSERT_ONCE(!"non-source file passed to ParseFileForGoToDef");
		}
#endif // !NDEBUG
		return;
	}

	const uint flags = uint(sysFile ? V_SYSLIB : V_INPROJECT);
	FormatFile(srcfile, flags, ParseType_GotoDefs, true, text, (uint)extraDbFlags);
}

void MultiParse::ParseFile(const CStringW& ifile, bool parseall, WTString& initscope, LPCSTR text /* = NULL */,
                           uint dbFlags /*= 0*/)
{
	DEFTIMER(ParseFileTimer);
	CatLog("Parser.FileName", " FormatFile::ParseFile");
	LOG("FormatFile::ParseFile");
	const CStringW lfile(ifile);
	Init();
	m_parseAll = parseall;
	OpenIdxOut(ifile, dbFlags);
	CLEARERRNO;

	FILETIME ftime;
	if (!GetFTime(lfile, &ftime))
	{
		Log("MPPF:GetFTimeFiled");
#if !defined(RAD_STUDIO) // in radstudio we parse file contents for files that haven't been saved to disk, so don't assert
		ASSERT(FALSE);
#endif
	}
	g_DBFiles.SetFileTime(m_fileId, m_VADbId, &ftime); // Update ftime before parse. case=23013

	WTString code = text ? WTString(text) : ReadFile(lfile);
	_ASSERTE(m_fileId == gFileIdManager->GetFileId(lfile));
	const CString fileId = gFileIdManager->GetFileIdStr(lfile);
	// this db entry is made so that we know if a file has been loaded/parsed - see IsIncluded()
	WTString fileDef;
	fileDef.WTFormat("ContentSize:%x;ContentHash:%s", code.GetLength(), ::GetTextHashStr(code).c_str());
	ImmediateDBOut(WTString(fileId), fileDef, UNDEF, V_FILENAME | mFormatAttrs | V_HIDEFROMUSER, 0);

	for (int i = 0; i < 3 && i < code.GetLength(); i++)
	{
		if (code[i] & 0x80)
		{
			// added in change 4428
			// what is this for???
			// maybe unicode BOM ?
			code.SetAt(i, ' ');
		}
	}

	if (code.Find('\n') == -1)
		code.ReplaceAll('\r', '\n'); // mac files
	int ftype = GetFileType(lfile);
	if (ftype == Idl)
	{
		// remove all [...]
		// code = UnIdlStr(code);
	}
	CatLog("Parser.MultiParse", (WTString("code.length() = ") + itos(code.length())).c_str());
	// don't parse if Reparse and !definition
	// why? //if(m_parseType != F_REPARSE || code.FindOneOf("{;#") != NPOS)
	ParseStr(code, WTString(initscope));
	FlushDbOutCache();
	CloseIdxOut();
	ClearTemporaryMacroDefs();
	ClearBuf();
}

WTString MultiParse::ParseStr(const WTString& code, const WTString& InitScope)
{
	DEFTIMER(ParseStrTimer);
	DB_READ_LOCK;
	m_p = code.c_str();
	m_len = code.length();
	m_cp = 0;

	// m_parseAll = false;
	// m_parseType = F_SCOPE;
	try
	{
		// 		ASSERT(InitScope.length());
		if (CAN_USE_NEW_SCOPE(FileType()))
		{
			if (m_parseAll)
				VAParseParseLocalsToDFile(code, shared_from_this(), GetCacheable());
			else
				VAParseParseGlobalsToDFile(code, shared_from_this(),
				                           GetCacheable()); // ParseBlock(0, InitScope, NULL, true);
		}
		else if (FileType() == RC)
			ParseBlockHTML();
		else if (FileType() == JS || FileType() == PERL /*|| FileType() == CS*/)
			ParseBlockHTML();
		else if (FileType() == Other)
		{
			ASSERT(FALSE);
			ReadTo("ENDOFTHEFILE", COMMENT);
		}
		else if (InitScope.length())
		{
			ASSERT(FALSE);
		}
		else
			m_lastScope += ":";
		return m_lastScope;
	}
	catch (const UnloadingException&)
	{
		VALOGEXCEPTION("MP-unloading:");
		throw;
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("MP:");
		return NULLSTR;
		/*
		        Log("ERROR: Exception in ParseStr");
		        // save code that caused dump
		        WTofstream errorfile(VaDirs::GetDbDir() + "VAerror.log");
		        errorfile << '"' << InitScope << "\" " << m_cp << ", " << m_len << "\n";
		        errorfile << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
		        errorfile << code;
		        errorfile << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
		        ASSERT(FALSE);
		        if(m_pCFile)	// oops coloring file failed, may be truncated, what do we do?
		            ErrorBox("Error parsing file.");
		        // else,  no big deal, rest of file wont me parsed
		        // allow to continue
		        return NULLSTR;
		*/
	}
#endif // !SEAN
}

bool MultiParse::ReadTo(const char* tostr, uint type, uint attrs)
{
	_ASSERTE((type & TYPEMASK) == type);
	size_t slen = strlen(tostr);
	BOOL ignoreBackSlash = FALSE;
#if !defined(SEAN)
	try
#endif // !SEAN
	{
		if (tostr[0] == '"')
			ignoreBackSlash = (m_p[m_cp - 2] == '@'); // ignoreBackSlash for c# @"...\n..."
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("MP:");
	}
#endif // !SEAN
	const char* s2 = tostr;

	char spellBuf[255];
	int spellBufPos = 0;
	BOOL ignoreWord = FALSE;

	for (; m_p[m_cp]; m_cp++)
	{
		if ((Psettings->m_spellFlags & 0x1000) && (m_spellFromPos < m_cp || m_showRed) // spell checking file
		)
		{
			BOOL doSpell = (m_cp == (m_len - 1));
			// [case: 1226] intention of condition is unclear; see change 12436
			if (!ignoreWord && (m_p[m_cp] == '\'' || ISALPHA(m_p[m_cp])) &&
			    '@' != m_p[m_cp]) // Changed to ISALPHA for accent chars like �. case=1226 case=22238
			{
				char c = m_p[m_cp];
				if (!spellBufPos)
					s_SpellBlockInfo.m_p1 = m_cp;
				if (spellBufPos && (c >= 'A' && c <= 'Z')) // Contains CAPS/mixed case
					ignoreWord = TRUE;
				if (spellBufPos < 254)
					spellBuf[spellBufPos++] = c;
				spellBuf[spellBufPos] = '\0';
			}
			else
			{
				spellBuf[spellBufPos++] = '\0';
				doSpell = TRUE;
				if (m_p[m_cp] == '%' && ISCSYM(m_p[m_cp + 1])) // %somestr% or %ul
					ignoreWord = TRUE;
				else if (m_p[m_cp] == '%' && ISCSYM(m_p[m_cp + 1])) // %somestr% or %ul
					ignoreWord = TRUE;
				else if (m_p[m_cp] == '.' && ISCSYM(m_p[m_cp + 1])) // www.something
					ignoreWord = TRUE;
				else if (m_p[m_cp] == '#' && ISCSYM(m_p[m_cp + 1])) // [case: 91351] hashtag
					ignoreWord = TRUE;

				if ((m_p[m_cp] == '\\') && (m_cp < (m_len - 1)) && strchr("btrfn", m_p[m_cp + 1]))
				{
					// do nothing -> don't ignore word if separator is character escape
				}
				else if (strchr("<@\\/-_", m_p[m_cp]) || wt_isdigit(m_p[m_cp]))
					ignoreWord = TRUE;

				spellBufPos = 0;
				if (strchr(" \t\r\n,\"", m_p[m_cp]))
					ignoreWord = FALSE;
				if (!ignoreWord)
				{
					// see if it looks like code, thiss * willl = nt+beee/Spelled;
					LPCSTR nc;
					for (nc = &m_p[m_cp]; *nc && strchr(" \t", *nc); nc++)
						;
					if (strchr("=<>*+;[]", *nc))
						ignoreWord = TRUE;
				}
			}

			if (!ignoreWord && doSpell)
			{
				WTString cwd(spellBuf);
				if (!m_pLocalDic->FindAny(cwd) && !g_pGlobDic->FindAny(cwd) && !SDictionary()->FindAny(cwd))
				{
					if (!FPSSpell(cwd.c_str()) &&
					    FPSSpellSurroundingAmpersandWord(m_p, m_cp - cwd.GetLength(), m_cp, cwd))
					{
						if (m_spellFromPos != -1)
						{
							s_SpellBlockInfo.spellWord = cwd;
							s_SpellBlockInfo.m_p2 = s_SpellBlockInfo.m_p1 + cwd.GetLength();
							m_len = m_cp - cwd.GetLength();
							return true;
						}
						m_pLocalDic->add(WTString(":VAunderline:") + cwd, itos(m_line), LVAR,
						                 V_PRIVATE | V_HIDEFROMUSER);
					}
				}
			}
		}

		if (m_p[m_cp] == '\n')
		{
			m_line++;
			// bailout if unmatched quote
			if (type == STRING && !ignoreBackSlash) // Ignore returns in @"comments"
				return false;
		}

		if (m_p[m_cp] == '\\' && type != COMMENT && !ignoreBackSlash)
		{
			m_cp++;
			if (m_p[m_cp] == '\r' || m_p[m_cp] == '\n') // for \r\n
			{
				// Found \r\r\n on multi-line macros?
				while (m_p[m_cp] == '\r')
					m_cp++;
				m_line++;
			}
		}
		else
		{
			if (m_p[m_cp] == '{' && type == ASM)
				s2 = "}";
			if (m_p[m_cp] == '/' && type == DEFINE)
			{
				if (m_p[m_cp + 1] == '/')
				{
					m_cp++; // so that view whitespace and background color settings work properly
					return ReadTo("\n", COMMENT);
				}
				else if (m_p[m_cp + 1] == '*')
				{
					m_cp++;                // so that view whitespace and background color settings work properly
					ReadTo("*/", COMMENT); // continue to close #define ...\n
					if (m_cp >= m_len)
						return false;
				}
			}
			if (strncmp(&m_p[m_cp], s2, slen) == 0)
			{
				m_cp += strlen_i(tostr);
				if (m_cp >= m_len && strcmp(tostr, "*/") == 0) // fix CurScope on  "* /"
				{
					m_type = type;
					mTypeAttrs = attrs;
				}
				return true;
			}
		}
	}
	m_scopeType = (int)type;
	// EOF reached, set m_lastScope
	m_type = type;
	mTypeAttrs = attrs;
	if (type == COMMENT)
	{
		if (strcmp(tostr, "\n") == 0)
			m_lastScope = "CommentLine";
		else
			m_lastScope = "CommentBlock";
		// m_lastScope = ":PP";
	}
	else if (type == STRING)
		m_lastScope = "String";
	else if (type == DEFINE)
		m_lastScope = ":PP:"; // "PreProc";
	else if (type == ASM)
		m_lastScope = "ASM"; // "PreProc";
	return false;
}

const WTString vam(":1:");

void AddSpecialCppMacros()
{
	_ASSERTE(g_pMFCDic && g_pMFCDic->m_loaded);
	const uint attrs = V_HIDEFROMUSER | V_DONT_EXPAND | V_VA_STDAFX;
	g_pMFCDic->add(vam + "VA_CPP_Tag", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "operator", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "case", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "for", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "__catch", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "_catch", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "catch", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "namespace", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "typedef", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "class", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "struct", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "union", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "__asm", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "_asm", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "asm", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "enum", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "operator", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "if", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "while", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "friend", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "return", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "delete", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "throw", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "restrict", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "new", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "goto", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "else", NULLSTR, VaMacroDefArg, attrs);
	g_pMFCDic->add(vam + "interface", WTString("struct"), VaMacroDefArg, attrs);
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	g_pMFCDic->add(":__classid", "TMetaClass* __classid(TObject)", FUNC, attrs | V_SYSLIB);
	g_pMFCDic->add(":__delphirtti", "TTypeInfo* __delphirtti(TObject)", FUNC, attrs | V_SYSLIB);
#endif
}

void AddSpecialCsMacros()
{
	_ASSERTE(g_pCSDic && g_pCSDic->m_loaded);
	const uint attrs = V_HIDEFROMUSER | V_DONT_EXPAND | V_VA_STDAFX;
	g_pCSDic->add(vam + "VA_CS_Tag", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "case", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "foreach", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "for", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "catch", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "namespace", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "class", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "struct", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "union", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "enum", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "if", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "while", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "friend", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "return", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "delete", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "new", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "goto", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "else", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "using", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "new", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "public", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "private", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "protected", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "internal", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "throw", NULLSTR, VaMacroDefArg, attrs);
	g_pCSDic->add(vam + "interface", WTString("class "), VaMacroDefArg, attrs);
}

void CheckForFreeSpace()
{
	// check for free space
	// only check if it hasn't been checked in the last N seconds
	static DWORD ltm = 0;
	static bool sContinueToReport = Psettings->m_resourceAlarm;
	if (sContinueToReport && (GetTickCount() - ltm) > 60000)
	{
		ULARGE_INTEGER userSpace;
		if (GetDiskFreeSpaceExW(VaDirs::GetDbDir(), &userSpace, NULL, NULL))
		{
			const int kMinMegabytes = 500;
			const int kThreshold = kMinMegabytes * (1024 * 1024);
			if (userSpace.QuadPart < kThreshold)
			{
				sContinueToReport = false;
				ErrorBox("Disk is low on space.");
			}
		}
		ltm = GetTickCount();
	}
}

#define TESTSTOP()            \
	if (StopIt || m_stop)     \
	{                         \
		SetStatus(IDS_READY); \
		return;               \
	}

void MultiParse::FormatFile(const CStringW ifile, uint attr, ParseType parseType, bool isReparse,
                            LPCSTR text /* = NULL */, uint dbFlags /*= 0*/)
{
	_ASSERTE(g_threadCount != 0);
	const int kSleepTime = 50;
	const int kMaxWait = 60000;
	bool skipIt = false;
	for (int cnt = 0;; ++cnt)
	{
		{
			AutoLockCs l(sActiveFormatFileListLock);
			if (sActiveFormatFileList.ContainsNoCase(ifile))
			{
				// wait until file is no longer active
				if (!text && ((ParseType_Globals == parseType && !isReparse) || ParseType_GotoDefs == parseType))
					skipIt = true; // no need to do global parse since it's already active

				if (kMaxWait < (cnt * kSleepTime))
					return; // [case: 58736]
			}
			else if (skipIt)
				return;
			else
			{
				sActiveFormatFileList.Add(ifile);
				break;
			}
		}

		::Sleep(kSleepTime);
	}

	try
	{
		DoFormatFile(ifile, attr, parseType, isReparse, text, dbFlags);
	}
	catch (const UnloadingException&)
	{
		VALOGEXCEPTION("MP-unloading:");
		AutoLockCs l(sActiveFormatFileListLock);
		sActiveFormatFileList.Remove(ifile);
		throw;
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("MP:");
		_ASSERTE(!"exception in MultiParse::FormatFile");
	}
#endif // !SEAN

	AutoLockCs l(sActiveFormatFileListLock);
	sActiveFormatFileList.Remove(ifile);
}

void MultiParse::DoFormatFile(const CStringW& ifile, uint attr, ParseType parseType, bool isReparse, LPCSTR text,
                              uint dbFlags)
{
	DEFTIMER(FormatFileTimer);

	ASSERT_LOOP(40);
	if (g_loggingEnabled)
	{
		WTString ns(GetGlobalNameSpaceString());
		ns.ReplaceAll("\f", ";");
		vCatLog("Parser.FileName", "FormatFile: %s tid(0x%lx) pt(%d) a(%x) r(%d) nslen(%d)\n", (LPCTSTR)CString(ifile), GetCurrentThreadId(),
		     parseType, attr, isReparse, ns.GetLength());
		vCatLog("Parser.FileName", " ns(%s)\n", ns.c_str());
	}
	CheckForFreeSpace();
	// elapsed time is not necessarily indicative of parsing only ifile -- parsing of dependencies may be included
	LogElapsedTime let("MP::DFF", ifile, 500);

	bool addedSysAttr = false;
	if ((attr & V_SYSLIB) || IncludeDirs::IsSystemFile(ifile))
	{
		if (!(attr & V_SYSLIB) && !dbFlags && parseType != ParseType::ParseType_Locals)
		{
			// this means IsSystemFile but we were called without V_SYSLIB;
			// so need to check for solution private sys
			if (IncludeDirs::IsSolutionPrivateSysFile(ifile))
			{
				vLog("overriding general sys as solution private sys %s", (LPCTSTR)CString(ifile));
				dbFlags = VA_DB_SolutionPrivateSystem | VA_DB_Cpp;
			}
		}

		m_isSysFile = TRUE;
		if (!(attr & V_SYSLIB))
		{
			attr |= V_SYSLIB;
			addedSysAttr = true;
		}
	}
	else
		m_isSysFile = FALSE;

	m_parseType = parseType;

	TESTSTOP();

	// StopIt = false;
	CStringW file = MSPath(ifile);
	if (XML != m_ftype) // [case: 77078]
		m_ftype = GetFileType(file);
	switch (FileType())
	{
	case Plain:
	case Tmp:
	case Other:
	case Binary:
	case SQL:
	case Image:
		return;
	case CS:
	case VB:
	case VBS:
		if (ParseType_Globals == m_parseType)
		{
			// [case: 13960]
			// when loading .NET framework metadata files, don't parse for globals.
			// system globals are already loaded via vanetobj* imports;
			// if globals were parsed from metadata, they are put into the cache db;
			// symbols found in cache db are not displayed in italic.
			// Using ShouldIgnoreFile to see if file is in temp dir.
			if (ShouldIgnoreFile(file, false))
			{
				// Metadata filenames are of the form:
				// tempDir\...$text$vNumber....
				int startPos = file.Find(L'$');
				if (-1 != startPos)
				{
					startPos = file.Find(L"$v", startPos);
					if (-1 != startPos)
						return;
				}
			}
		}
		break;
	case Src:
	case Header:
		if (Psettings->mUnrealEngineCppSupport)
		{
			if (Psettings->mIndexPlugins < 4 && IsUEIgnorePluginsDir(file)) // [case: 141741]
				return;

			if (!Psettings->mIndexGeneratedCode && EndsWithNoCase(file, L".generated.h")) // [case: 119653]
				return;
		}
		if (GlobalProject && !GlobalProject->GetFileExclusionRegexes().empty())
		{
			for (const auto& regex : GlobalProject->GetFileExclusionRegexes())
			{
				if (std::regex_match(std::wstring(file), regex))
				{
					// [case: 149171] don't parse excluded files
					return;
				}
			}
		}
		break;
	default:
		break;
	}

	if (::IsRestrictedFileType(m_ftype))
		return;

	SDictionary(); // = GetSysDic(FileType());
	SetFilename(file);
	m_fileId = gFileIdManager->GetFileId(file);

	mFormatAttrs = attr;
	ASSERT(m_isSysFile == ((attr & V_SYSLIB) != 0));

	WTString initscope;
#if defined(RAD_STUDIO)
	WTString backingTextForUnsavedFile;
#endif
	TESTSTOP();
	if (!IsFile(ifile))
	{
#if defined(RAD_STUDIO)
		if (!text)
		{
			// in radstudio we parse file contents for files that haven't been saved to disk if we
			// have been given the content via the plugin
			backingTextForUnsavedFile = PkgGetFileTextW(file);
			if (backingTextForUnsavedFile.GetLength())
				text = backingTextForUnsavedFile.c_str();
		}

		if (!gVaRadStudioPlugin || !text)
		{
			Log((const char*)(CString("Error: Can not open file '") + CString(ifile) + "'."));
			return;
		}
#else
		LogUnfiltered((const char*)(CString("Error: Can not open file '") + CString(ifile) + "'."));
		return;
#endif
	}

	m_parseAll = (parseType == ParseType_Locals);

	// Make sure file is not already in there
	if (!m_parseAll)
	{
		if (!(mFormatAttrs & V_VA_STDAFX))
		{
			if (addedSysAttr && V_INPROJECT)
			{
				if (SDictionary()->GetFileData(file))
				{
					// [case: 141708]
					// don't parse project file that will end up in sysdb if it is already there
					return;
				}
			}

#ifdef _DEBUG
			bool filedataAlreadyExists;
			if (m_isSysFile)
				filedataAlreadyExists = !!SDictionary()->GetFileData(file);
			else
				filedataAlreadyExists = !!g_pGlobDic->GetFileData(file);
			_ASSERTE(!filedataAlreadyExists);
#endif
		}
	}

	// make sure permissions ok on tmp files...
	SetStatus(IDS_READING_FNAME, (LPCTSTR)CString(file));
	// temporarily reset m_showred - fixes calls to GetBaseClassList
	//   for classes defined locally within .cpp files
	//   like class MyDialog : public CDialog in FindRep.cpp
	BOOL showred = m_showRed;
	m_showRed = false;
	m_VADbId = g_DBFiles.DBIdFromParams(FileType(), m_fileId, m_isSysFile, m_parseAll,
	                                    mIsVaReservedWordFile ? VA_DB_ExternalOther : dbFlags);

	FILETIME* ft = g_DBFiles.GetFileTime(m_fileId, m_VADbId);
	if (!ft || (!ft->dwHighDateTime && !ft->dwLowDateTime))
		ft = NULL;

	if (parseType == ParseType_Locals)
	{                       // parse all only if coloring
		ASSERT(m_parseAll); // should be already set from above
		m_parseAll = true;
		if (isReparse || !ft || !FileTimesAreEqual(ft, file))
		{
			ParseFile(file, true, initscope, text, dbFlags);
		}
	}
	else
	{
		if (!ShouldParseGlobals(FileType()) && FileType() != Src && FileType() != RC)
		{
			return;
		}
		m_parseAll = false;
		// use file in tmp if possible
		if (isReparse || /*text ||*/ !ft /*|| !FileTimesAreEqual(file, dx_file)*/)
		{
#ifdef _DEBUG
			if (ft)
			{
				CatLog("Parser.FileName", (const char*)(CString(file) + " Exists2"));
			}
			else
			{
				CatLog("Parser.FileName", (const char*)(CString(file) + " Gone2"));
			}
#endif // _DEBUG
			ParseFile(file, false, initscope, text, dbFlags);
		}
	}
	m_showRed = showred;
	CatLog("Parser.FileName", (const char*)(CString("Loading ") + CString(file)));
	TESTSTOP();

#ifdef _DEBUG
	SetStatus((const char*)(CString("DbugMsg: Loading ") + CString(file)));
#endif

	// load file created by previous call to ParseFile
	ReadIdxFile();

	TESTSTOP();
	if (parseType == ParseType_Globals || parseType == ParseType_Locals)
	{
		SetStatus(IDS_READY);
		return;
	}
	TESTSTOP();
	SetStatus(IDS_READY);

	if ((g_pMFCDic && !g_pMFCDic->m_loaded) || (g_pCSDic && !g_pCSDic->m_loaded))
		return;

	RealWrapperCheck chk(g_pUsage->mFilesMParsed, 400);
}

void MultiParse::Stop()
{
	m_stop = true;
}

void MultiParse::SpellBlock(EdCnt* ed, long from, long to)
{
	Init();
	// m_stop = true; whats this?
	m_p = ed->GetBuf().c_str();
	m_len = ed->GetBuf().length();
	for (; to < m_len && (ISCSYM(m_p[to]) || m_p[to] == '\''); to++)
		; // fo to end of reverse selected word
	if (to && to < m_len)
		m_len = to;
	to = m_len;
	m_spellFromPos = from;
	m_showRed = FALSE;
	TempAssign<EdCnt*> ta(m_ed, ed, nullptr);
	m_parseAll = TRUE;
	int spellto = to;
	//	if(from)
	//		ReadTo("*/");
	Psettings->m_spellFlags |= 0x1000; // set spelling flag
	m_ftype = ed->m_ftype;
	if (FileType() && (SDictionary() != (Defaults_to_Net_Symbols(FileType()) ? g_pCSDic : g_pMFCDic)))
	{
		if (Defaults_to_Net_Symbols(FileType()) || IsCFile(FileType()))
		{
			_ASSERTE(!"SDictionary mismatch in SpellBlock");
		}

		SDictionary();
	}
	s_SpellBlockInfo.Init();
	if (Is_VB_VBS_File(FileType()) || Is_Tag_Based(FileType()))
		ParseBlockHTML();
	else if (FileType() == Other || FileType() == Plain)
		ReadTo("ENDOFTHEFILE", COMMENT);
	else
		qParseBlock();

	while (m_cp && m_cp < spellto && m_cp < ed->GetBuf().length())
	{
		// found misspelling
		ed->SetSel((long)s_SpellBlockInfo.m_p1, (long)s_SpellBlockInfo.m_p2);
		token2 t = s_SpellBlockInfo.spellWord;
		t.ReplaceAll("&", "");
		const WTString txt = t.GetLength() ? SpellWordDlg(t.c_str().c_str()) : L"";
		if (!txt.IsEmpty())
		{
			if (strcmp(txt.c_str(), s_SpellBlockInfo.spellWord.c_str()) != 0)
			{
				//				spellto += m_firstWord.length() - strlen(txt);
				spellto += txt.GetLength() - s_SpellBlockInfo.spellWord.GetLength();
				ed->InsertW(txt.Wide());
				m_p = ed->GetBuf().c_str();
			}
			m_spellFromPos = m_len + txt.GetLength();
			m_len = spellto;
			m_cp = 0;
			s_SpellBlockInfo.Init();
			if (m_len > ed->GetBuf().length())
				m_len = ed->GetBuf().length();
			if (Is_VB_VBS_File(FileType()) || Is_Tag_Based(FileType()))
				ParseBlockHTML();
			else if (FileType() == Other || FileType() == Plain)
				ReadTo("ENDOFTHEFILE", COMMENT);
			else
				qParseBlock();
		}
		else
		{
			Psettings->m_spellFlags &= 0x0fff; // reset spelling flag
			ClearBuf();
			return; // user hit cancel
		}
	}
	Psettings->m_spellFlags &= 0x0fff; // reset spelling flag
	ed->Invalidate(TRUE);
	WtMessageBox(ed->GetSafeHwnd(), "Spell check complete.", IDS_APPNAME, MB_OK);
	ClearBuf();
}

int MultiParse::qParseBlock()
{
	while (m_cp < m_len)
	{
		m_cp++;
		switch (m_p[m_cp - 1])
		{
		case '/':
			if (m_p[m_cp] == '/')
			{
				ReadTo("\n", COMMENT);
			}
			else if (m_p[m_cp] == '*')
			{
				ReadTo("*/", COMMENT);
			}
			break;
		case '#':
			AddPPLn();
			// ReadTo("\n");
			break;
		case '(':
		case '[':
		case '{':
			qParseBlock();
			break;
		case ')':
		case ']':
		case '}':
			return m_p[m_cp - 1];
		case '"':
			ReadTo("\"", STRING);
			break;
		case '\'':
			ReadTo("'", STRING);
			break;
		case '\n':
			m_line++;
			break;
		}
	}
	return 0;
}

void MultiParse::AddDef(const WTString& sym, WTString& def, uint itype, uint attrs, uint dbFlags)
{
	_ASSERTE((itype & TYPEMASK) == itype);
	_ASSERTE((dbFlags & VA_DB_FLAGS) == dbFlags);

	if (def.length() > MAXLN)
	{
		CStringW tmp(def.Wide());
		tmp = tmp.Mid(0, MAXLN - 10) + L"...";
		def = (const wchar_t*)tmp;
	}

	if ((attrs & V_INFILE) || (attrs & V_LOCAL)) // Fixes bug where all c# parameters we added to the global dictionary
		add(sym, def, DLoc, itype, attrs, dbFlags);
	else if (attrs & V_SYSLIB)
		add(sym, def, DSys, itype, attrs, dbFlags);
	else
		add(sym, def, DMain, itype, attrs, dbFlags);
}

void MultiParse::AddStuff(const WTString& sym, WTString& def, uint itype, uint attrs, uint dbFlags)
{
	_ASSERTE((itype & TYPEMASK) == itype);
	_ASSERTE((dbFlags & VA_DB_FLAGS) == dbFlags);
	DEFTIMER(AddStuff1);

	_ASSERTE(sym[0] != '+'); // got rid of old MACROCHAR check (2007.11.20)

	if (((itype & 0x1f) == DEFINE) && def[0] == '#')
	{
		bool doProcIncLn = false;
		if (def.contains("#include"))
			doProcIncLn = true;
		else if (def.contains("#import"))
		{
			// This is similar to the code in mparsepp.cpp for #import(s)
			// mparsepp adds the import args to the def - extract here and
			//   add them to g_pGlobDic
			// The code in mparsepp isn't enough - problems when loading from cache
			const char* p = def.c_str();
			p = strstr(p, "import");
			p += 6;
			while (*p == ' ' || *p == '\t')
				p++;
			// get past filename
			int i;
			for (i = p[0] ? 1 : 0; p[i] && p[i] != '"' && p[i] != '>'; i++)
				;
			if (p[i])
			{
				// #import file import args(namespace and rename stuff)
				// save import args so Import() can use them to generate .tlh file
				WTString bname;
				for (int n = 1; n < i; n++)
					bname += (p[n] & 0x80) ? p[n] : (char)tolower(p[n]);
				g_pGlobDic->add(Basename(bname) + "_import_args", WTString(&p[i + 1]), DEFINE);
			}
			doProcIncLn = true;
		}

		if (doProcIncLn)
		{
			ProcessIncludeLn(def, itype, attrs);
			return;
		}
	}

	AddDef(sym, def, itype, attrs, dbFlags);
}

byte* ReadBinFile(const CStringW& file, DWORD& len)
{
	CFileW idxFile;

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		if (idxFile.Open(file, CFile::modeRead | CFile::typeBinary | CFile::shareDenyNone))
		{
			len = (DWORD)idxFile.GetLength();
			byte* buf = new byte[len];
			if (buf)
				idxFile.Read(buf, len);
			else
			{
				vLog("ERROR: ReadBinFile alloc failed %s\n", (LPCTSTR)CString(file));
			}
			return buf;
		}
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("MP:");
	}
#endif // !SEAN
	len = 0;
	return NULL;
}

static const DWORD kFilesizeLoggingThreshold = 5 * 1024 * 1024;

void MultiParse::ReadIdxFile()
{
	// Get idx from DBFile
	DWORD sz;
	UINT usz;

	LPVOID data = g_DBFiles.ReadIdxFile(m_fileId, m_VADbId, usz);
	sz = (DWORD)usz;
	if (data)
	{
		if (sz > kFilesizeLoggingThreshold && g_loggingEnabled)
		{
			WTString msg;
			msg.WTFormat("size threshold: MP::ReadIdxFile %d %lu", m_VADbId, sz);
			vLog("%s", msg.c_str());
		}
		ReadIdxData(data, sz, TRUE);
		free(data);
	}
}

void MultiParse::ReadDBFile(const CStringW& dbFile)
{
	// Read whole DBFile
	DEFTIMER(ReadIdxFile1);
	DWORD sz;
	byte* data = ::ReadBinFile(dbFile, sz);
	if (data)
	{
		if (sz > kFilesizeLoggingThreshold && g_loggingEnabled)
		{
			WTString msg;
			msg.WTFormat("size threshold: MP::ReadDBFile %s %lu", WTString(dbFile).c_str(), sz);
			vLog("%s", msg.c_str());
		}

		ReadIdxData(data, sz, FALSE);
		delete[] data;
	}
}

void MultiParse::ProcessPendingIncludes(uint dbFlags)
{
	// [case: 97154]
	// parse files that were included by files previously parsed but that
	// don't appear to have been loaded during LoadIdxesInDir.
	//
	// Watch out for the effect that having Psettings->mSparseSysLoad enabled
	// has on this parsing -- results will not be loaded by LoadIdxesInDir.
	// The includes will be parsed on every solution load. (see case 97190)

	FileList files;

	{
		AutoLockCs l(mPendingParseLock);
		files.swap(mPendingIncludes);
	}

	auto processor = [dbFlags](const FileInfo& fi) {
		if (gShellIsUnloading || StopIt)
			return;

		DType* fileData = GetSysDic()->GetFileData(fi.mFilename);
		if (fileData)
			return; // already parsed

		fileData = g_pGlobDic->GetFileData(fi.mFilename);
		if (fileData)
			return; // already parsed

		try
		{
			MultiParsePtr mp = MultiParse::Create();
			uint attr = 0;
			uint flags =
			    dbFlags; // the flags for the file that was being parsed when the include directives were encountered
			if (dbFlags & VA_DB_SolutionPrivateSystem)
			{
				attr = V_SYSLIB;
				// [case: 132428]
				// just because a VA_DB_SolutionPrivateSystem file included another file
				// doesn't mean the included file is also VA_DB_SolutionPrivateSystem.
				if (IncludeDirs::IsSystemFile(fi.mFilename))
				{
					if (!IncludeDirs::IsSolutionPrivateSysFile(fi.mFilename))
					{
						vLog("WARN: dbFlags override 1 in MP:PPI: %s", WTString(fi.mFilename).c_str());
						flags = VA_DB_Cpp;
					}
				}
				else if (GlobalProject && GlobalProject->Contains(fi.mFilename))
				{
					attr = V_INPROJECT;
					flags = VA_DB_Solution;
					vLog("WARN: dbFlags override 2 in MP:PPI: %s", WTString(fi.mFilename).c_str());
				}
				else
				{
					// ??; original, possibly incorrect, behavior
					vLog("WARN: dbFlags 2 in MP:PPI: %s", WTString(fi.mFilename).c_str());
				}
			}
			else if (dbFlags & VA_DB_Cpp)
			{
				attr = V_SYSLIB;
			}
			else if (dbFlags & VA_DB_Solution)
			{
				_ASSERTE(VA_DB_Solution == dbFlags);
				if (IncludeDirs::IsSystemFile(fi.mFilename))
				{
					attr = V_SYSLIB;
					flags = VA_DB_Cpp;
					vLog("WARN: dbFlags override 3 in MP:PPI: %s", WTString(fi.mFilename).c_str());
				}
			}
			else
			{
				vLog("ERROR: unhandled dbFlags in MP:PPI: %s", WTString(fi.mFilename).c_str());
				_ASSERTE(!"unhandled dbFlags in MultiParse::ProcessPendingIncludes");
				return;
			}

			mp->FormatFile(fi.mFilename, attr, ParseType_Globals, false, nullptr, flags);
		}
#if _MSC_VER <= 1200
		catch (CException e)
		{
			VALOGEXCEPTION("MP:PPI:");
			_ASSERTE(!"exception in ProcessPendingIncludes");
			Log("oops1 ppi");
		}
#endif
		catch (const UnloadingException&)
		{
		}
#if !defined(SEAN)
		catch (...)
		{
			// ODS even in release builds in case VALOGEXCEPTION causes an exception
			CString msg;
			CString__FormatA(msg, "VA ERROR exception caught MP:PPI: %d\n", __LINE__);
			OutputDebugString(msg);
			_ASSERTE(!"ProcessPendingIncludes exception");
			VALOGEXCEPTION("MP:PPI:");
		}
#endif // !SEAN
	};

	if (Psettings->mUsePpl)
		Concurrency::parallel_for_each(files.cbegin(), files.cend(), processor);
	else
		std::for_each(files.cbegin(), files.cend(), processor);

	_ASSERTE(!mPendingIncludes.size());
}

int MultiParse::ReadDFile(const CStringW& file, uint addlAttrs, uint addlDbFlags, char sep)
{
	DEFTIMER(ReadDFileTimer);
	LOG2("MParse::ReadDFile");
	int count = 0;
	uint itype = 0;

	WTString tdef;
	if (StopIt)
		return FALSE;

	if ((addlDbFlags & VA_DB_Net) && !GlobalProject->ShouldLoadNetDb())
		return FALSE;

	{
		// add to list to prevent recursion
		AutoLockCs l(sActiveDfileListLock);
		if (sActiveDfileList.ContainsNoCase(file))
			return false;

		sActiveDfileList.Add(file);
	}

	DB_READ_LOCK;
	BOOL doDBOut = file.Find(L".VA") == -1;
	if (doDBOut)
	{
		m_isSysFile = TRUE;
		m_parseAll = FALSE;
		OpenIdxOut(file, addlDbFlags);
	}

	DEFTIMER(ReadDFileTimer2);
	WTString fBuf;
	fBuf.ReadFile(file, -1, true);
	LONG fLen = fBuf.GetLength();
	if (fLen)
	{
		if ((DWORD)fLen > kFilesizeLoggingThreshold && g_loggingEnabled)
		{
			WTString msg;
			msg.WTFormat("size threshold: MP::ReadDFile %s %lu", WTString(file).c_str(), fLen);
			vLog("%s", msg.c_str());
		}

		WTString sym, def, type;
		LPTSTR pBegin, pEnd = NULL;
		pBegin = fBuf.GetBuffer(0);

		DEFTIMER(ReadDFileTimer3);

		if (m_parseAll)
			mGotoMarkers.reset(new LineMarkers);

		while (*pBegin)
		{
#if !defined(SEAN)
			try
#endif // !SEAN
			{
				if (m_stop || StopIt)
				{
					AutoLockCs l(sActiveDfileListLock);
					sActiveDfileList.Remove(file);
					return false;
				}
				count++;
				pEnd = (LPTSTR)strchr(pBegin, sep);
				if (!pEnd)
				{
					break;
				}
				*pEnd = '\0';
				sym = pBegin;
				pBegin = pEnd + 1;

				pEnd = (LPTSTR)strchr(pBegin, sep);
				if (!pEnd)
				{
					ASSERT(FALSE);
					_wremove(file);
					break;
				}
				*pEnd = '\0';
				def = pBegin;
				pBegin = pEnd + 1;

				pEnd = (LPTSTR)strchr(pBegin, sep);
				if (!pEnd)
				{
					ASSERT(FALSE);
					break;
				}
				*pEnd = '\0';
				type = pBegin;
				pBegin = pEnd + 1;

				if (sscanf(type.c_str(), "%x", &itype) < 1)
				{
					_ASSERTE(!"Failed to read DFile type");
					itype = 0;
				}
				const uint dbFlags = (itype & VA_DB_FLAGS) | addlDbFlags;
				_ASSERTE((itype & VA_DB_FLAGS_MASK) == (itype & TYPEMASK));
				itype &= TYPEMASK;

				pEnd = (LPTSTR)strchr(pBegin, sep);
				if (!pEnd)
				{
					ASSERT(FALSE);
					break;
				}
				*pEnd = '\0';
				type = pBegin;
				pBegin = pEnd + 1;

				if (*pBegin != sep)
				{
					pEnd = (LPTSTR)strchr(pBegin, sep);
					if (!pEnd)
					{
						ASSERT(FALSE);
						break;
					}
					m_line = atoi(pBegin);
					pBegin = pEnd + 1;
				}
				else
				{
					// VaNetObj does not output line numbers.
					// use order of entries in .d file as pseudo-line number for
					// consistent sorting of items regardless of HashTable row size.
					++m_line;
				}

				uint attrs = 0;
				sscanf(type.c_str(), "%x", &attrs);
				attrs |= addlAttrs;

				if (itype == COMMENT)
				{
					if (m_isSysFile && !m_parseAll)
						SDictionary()->add(sym, def, COMMENT, 0, 0, m_fileId, m_line);
					else
						g_pGlobDic->add(sym, def, COMMENT, 0, 0, m_fileId, m_line);
				}
				else
				{
					if (sym.Find('<') != -1)
					{
						// [case: 24739]
						// encode generics but not our "< >" (so that our MakeTemplate works)
						// or the "<>" metadata (so that we ignore it)
						if (sym.Find(":< >") == -1 && sym.Find(":<>") == -1)
							::EncodeTemplates(sym);
					}

					if (doDBOut)
					{
						// write to dbfile (in which case this function just serves to
						// convert from .d1 file to dbfile)
						DBOut(sym, def, itype, attrs, m_line);
					}
					else
					{
						// insert directly into hashtables -- no intermediate serialization
						// (for .va files)
						AddStuff(sym, def, itype, attrs, dbFlags);
					}
				}

				pEnd = (LPTSTR)strchr(pBegin, '\n');
				if (!pEnd) // normal break condition?
					break;
				pBegin = pEnd + 1;
			}
#if !defined(SEAN)
			catch (...)
			{
				VALOGEXCEPTION("MP:");
				ASSERT(FALSE);
				// one def not loaded, continue...
				Log("Exception caught loading one def in ReadDFile\n");
			}
#endif // !SEAN
		}

		fBuf.Empty();
	}

	if (doDBOut)
	{
		_ASSERTE(addlAttrs & V_SYSLIB && addlDbFlags & VA_DB_Net);
		if (addlAttrs == V_SYSLIB && addlDbFlags == VA_DB_Net && sep == (char)0x01)
		{
			// called by NetImportDll
			// vaInheritsFrom types are cached in the .d1 file .net asm export.
			// VaNetObj exports that type along with all other types in the same output.
			// rather than make a separate export, just handle here by doing a purge
			// and repopulate of the inheritance tables.
			if (mDbOutInheritsFromCache.size())
			{
				std::set<UINT> fids;
				for (auto& dt : mDbOutInheritsFromCache)
					fids.insert(dt.mFileId);

				if (fids.size())
					InheritanceDb::PurgeInheritance(fids, DTypeDbScope::dbSystem);
			}
		}

		FlushDbOutCache();
		CloseIdxOut();

		// now load the db file
		ReadIdxFile();
	}

	// reload size of largest method, used in Scope();
	AutoLockCs l(sActiveDfileListLock);
	sActiveDfileList.Remove(file);

	return true;
}

void MultiParse::AddResword(const WTString& sym)
{
	WTString space(" ");
	AddStuff(sym, space, RESWORD, V_LOCAL, 0);
}

// Test to see if file is already included
// we do not need to check date here since it was checked when it was loaded
// in FormatFile.
bool MultiParse::IsIncluded(const CStringW& file, BOOL checkDate /*= FALSE*/)
{
	if (checkDate)
	{
		const UINT fileID = gFileIdManager->GetFileId(file);
		FILETIME* ft = g_DBFiles.GetFileTime(fileID, DbTypeId_Error);
		return (ft && FileTimesAreEqual(ft, file));
	}

	// Passed file should be full path
#ifdef _DEBUG
	if (file.CompareNoCase(MSPath(file)) != 0)
		_ASSERTE(!"MultiParse::IsIncluded file is not a full path");
#endif // _DEBUG

	// if an entry exists in either the sys or proj hashtables, the file has been parsed/loaded
	if (GetFileData(file))
		return true;
	return false;
}

void DoParseGlob(LPVOID file)
{
	if (gShellIsUnloading)
		return;

	try
	{
		MultiParsePtr mp = MultiParse::Create();
		mp->FormatFile(CStringW((LPCWSTR)file), 0, ParseType_Globals);
	}
#if _MSC_VER <= 1200
	catch (CException e)
	{
		VALOGEXCEPTION("MP:");
		ASSERT(FALSE);
		Log("oops1");
	}
#endif
	catch (const UnloadingException&)
	{
		throw;
	}
#if !defined(SEAN)
	catch (...)
	{
		// ODS even in release builds in case VALOGEXCEPTION causes an exception
		CString msg;
		CString__FormatA(msg, "VA ERROR exception caught MP:PG2: %d\n", __LINE__);
		OutputDebugString(msg);
		_ASSERTE(!"DoParseGlob exception");
		VALOGEXCEPTION("MP:PG2:");
	}
#endif // !SEAN
}

void ParseGlob(LPVOID file)
{
	const int kCurDepth = ++sDeep();
	try
	{
		// This prevents stack overflow when a file #includes a file which #includes a file ... and so on
		// once we get more than MAXDEEP, start another thread so we get a new stack
		// then wait for that thread to finish.

		// Not really sure of the number here, but with the new parser, 7 caused an overflow.
		// reduced to 4 and it seems OK.

		// Subsequently modified to check available stack space after 4 recursions.

		// build 1845 wer event id -1785954160 showed stackspace check wasn't checking
		// for enough available space.
		// Increased required available space and restored upper limit to recursion (though
		// higher than previously).
#define MAXDEEP 10
		if (kCurDepth > MAXDEEP || (kCurDepth > 4 && !HasSufficientStackSpace()))
		{
			FunctionThread thrd(DoParseGlob, file, "DoParseGlob", false);
			while (!thrd.HasStarted())
				Sleep(10);
			while (thrd.Wait(100) == WAIT_TIMEOUT)
			{
				//				_asm nop;
				volatile int i = 0;
				i = i + 1;
			}
		}
		else
			DoParseGlob(file);
	}
	catch (const UnloadingException&)
	{
		throw;
	}
#if !defined(SEAN)
	catch (...)
	{
		// ODS even in release builds in case VALOGEXCEPTION causes an exception
		CString msg;
		CString__FormatA(msg, "VA ERROR exception caught MP:PG %d\n", __LINE__);
		OutputDebugString(msg);
		_ASSERTE(!"ParseGlob exception");
		VALOGEXCEPTION("MP:PG:");
	}
#endif // !SEAN
	--sDeep();
}

bool MultiParse::ProcessIncludeLn(const WTString& ln, uint itype, uint attrs) // "#include <file.h>/"file.h"
{
	_ASSERTE((itype & TYPEMASK) == itype);
	DEFTIMER(ParseIncludeLineTimer);
	LOG2("MParse::ProcessIncludeLn");
	WTString def;
	token2 t = ln;
	t.read("\"<");                          // strip #include
	CStringW f = t.read("\"<>\r\n").Wide(); // get file.h
	if (f.IsEmpty())                        // leave if #include "" or #include <>
		return true;
	int ftype = GetFileType(f);
	MultiParsePtr mp = MultiParse::Create(shared_from_this(), ftype);
	// TODO: Test For already included...
	BOOL doLocalSearch = FALSE;

	DEFTIMER(ParseIncludeLineTimer2);
	// watch out for system headers with local includes ("file.h") - problem w/ 3rd party libs
	if (ln.contains("\"") && !(attrs & V_SYSLIB)) // "file.h"
	{
		// if m_fileName is an mfc src file that contains
		//  #include "stdafx.h", make sure we get its local copy
		//  before getting the current project's copy
		doLocalSearch = TRUE;
	}

	// see if syslib already included, no need to test date
	if (!doLocalSearch)
	{
		WTString fstr;
		fstr.WTFormat("*%s", WTString(f).c_str());
		fstr.MakeLower();
		WTString fpath = mp->def(fstr);
		if (fpath.GetLength())
			return TRUE; // already included
		// don't add to sys dic in case its not really found -Jer
		// add to global dic so we don't try to find this file again this session.
		g_pGlobDic->add(fstr, WTString("included"), VAR, V_SYSLIB, 0, m_fileId, -1);
	}

	DEFTIMER(ParseIncludeLineTimer3);
	f = gFileFinder->ResolveInclude(f, ::Path(GetFilename()), doLocalSearch);
	if (f.GetLength())
	{
		if (Binary == ftype)
		{
			if (!gShellAttr->SupportsCImportDirective())
				return false; // Import only works if vc6 is installed.
			f = Import(f);
		}
		if (IsIncluded(f))
		{
			if (Binary == ftype)
			{
				// file time of .tlh will still match the binary if all the user
				//   did was change #import args
				// if the args changed, it's a new tlh file with the same old time
				// Import can't change the file time (couldn't figure it out)
				//   we have recorded so it deleted the data file in this case
				// MarkIncluded doesn't check for existence of file
				// It might say file is up to date even though the .d file does not exist

				// TODO: will imports only be GlobalDataFiles?
				mp->FormatFile(f, V_SYSLIB, ParseType_Globals);
				return TRUE;
			}
			else
			{
				return TRUE; // already loaded
			}
		}
		if (ftype == RC)
		{
			// scenario: project has a header file that's not used by any
			//	source files.
			//  the resource file includes the header.
			//  the header itself includes other rc files.
			//  So the header is not a c/cpp file but an RC file with a .h extension
			vLog("WARN: (%s) MP::ProcIncLn skipping (%s)", (LPCTSTR)CString(GetFilename()), (LPCTSTR)CString(f));
			return true;
		}
		else
		{
			// don't add #import(s) to project file cache since
			//   the cache file doesn't keep the #import args
			DEFTIMER(ParseIncludeLineTimer4);
			ParseGlob((LPVOID)(LPCWSTR)f);
		}
		return true;
	}
	else if (-1 != f.Find(L"/*"))
	{
		_ASSERTE(!"tell sean if this happens (\"/*\") - send callstack");
		// 		WTString dir = f.Mid(0, f.Find("/*"));
		// 		dir.MakeLower();
		// 		DB_READ_LOCK;
		// 		if(!g_pGlobDic->Find(dir))
		// 		{
		// 			g_pGlobDic->add(dir, WTString("INCLUDED"), VAR);
		//
		// 			extern int FindDir (WTString& dirName, LPCTSTR extraPath, BOOL searchLocalPaths, BOOL
		// searchBinaryPaths); 			if(FindDir(dir, Path(GetFilename()), doLocalSearch, ftype == Binary ? TRUE :
		// FALSE)==0)
		// 			{
		// 				extern WTString FindFiles(const TCHAR* searchDir, const TCHAR* spec);
		// 				token2 files = FindFiles(dir, "*.java");
		// 				while (files.more()>2)
		// 				{
		// 					WTString f = files.read(";");
		// 					f = MSPath(f);
		// 					if(!IsIncluded(f))
		// 					{
		// 						mp.FormatFile(f, V_SYSLIB, ParseType_Globals);
		// 						return TRUE;
		// 					}
		// 				}
		// 			}
		// 		}
	}

	vLogUnfiltered("MultiParse::ProcessIncludeLn ERROR file not found %s", (LPCTSTR)CString(f));
	return false;
}

void MultiParse::add(const WTString& s, const WTString& d, DictionaryType dict /*= DMain*/, uint type /*= VAR*/,
                     uint attrs /*= 0*/, uint dbFlags /*= 0*/, uint fileId /*= 0*/, int line /*= 0*/)
{
	_ASSERTE((type & TYPEMASK) == type);
	DEFTIMER(AddTimer);
	_ASSERTE(0 < m_ftype && kLanguageFiletypeCount > m_ftype);

	// let sean know if you hit this
	_ASSERTE(type != VaMacroDefArg && type != VaMacroDefNoArg);

	if ((s.IsEmpty()) || (d.IsEmpty() && type != DEFINE))
		return; // false;

	if (!line)
		line = m_line;
	if (!fileId)
		fileId = m_fileId;

	if (DSys != dict)
	{
		if (m_isSysFile && !m_parseAll)
			attrs |= V_SYSLIB;
	}

	switch (dict)
	{
	case DSys:
		SDictionary()->add(s, d, type, V_SYSLIB | attrs, dbFlags, fileId, line);
		break;
	case DLoc:
		if ((FileType() == Src || FileType() == UC))
		{
			m_pLocalHcbDic->add(s, d, type, attrs, dbFlags, fileId, line);
			if (type != CachedBaseclassList)
				m_pLocalDic->add(s, d, type, attrs, dbFlags);
		}
		else
			m_pLocalDic->add(s, d, type, attrs, dbFlags);
		break;
	case DMain:
	default:
		g_pGlobDic->add(s, d, type, attrs, dbFlags, fileId, line);
		break;
	}
}

WTString MultiParse::GetBaseClassList(DType* pDat, const WTString& bc, bool force /*= false*/,
                                      volatile const INT* bailMonitor /*= NULL*/, int scopeLang /*= 0*/)
{
	if (bc.GetLength() < 2 || !m_showRed)
		return NULLSTR;

	DEFTIMER(MpBclTimer);
	LogElapsedTime let("MP::GBCL", bc, 50);
	BaseClassFinder bcf(scopeLang ? scopeLang : m_ftype);
	DB_READ_LOCK;
	WTString blc(bcf.GetBaseClassList(shared_from_this(), pDat, bc, force, bailMonitor));

	if (m_xref)
	{
		// [case: 56363] watch out for inadvertent members list pollution
		if (m_xrefScope == bc)
		{
			const DTypePtr cwData = GetXrefCwData();
			if (cwData && IS_OBJECT_TYPE(cwData->type()))
				blc += GetNameSpaceString(bc);
		}
	}

	if (Is_HTML_JS_VBS_File(scopeLang))
		blc += GetGuessedBaseClass(bc);

	return blc;
}

WTString MultiParse::GetBaseClassListCached(const WTString& bc)
{
	DType* cd = FindCachedBcl(kBclCachePrefix + bc);
	WTString def(bc);
	if (cd)
	{
		def = cd->Def();
		if (def.GetLength() && def[0] == '+' && def == kInvalidCache)
			return bc;
	}
	return def;
}

DType* MultiParse::FindOpData(WTString opStr, WTString key, volatile const INT* bailMonitor)
{
	LogElapsedTime let("MP::FOD", key, 100);
	WTString bcl(GetBaseClassList(key, false, bailMonitor));
	DType* opData = FindSym(&opStr, NULL, &bcl);
	if (opData && opStr == "->")
	{
		// [case: 12798][case: 8873] handle implicit operator-> chaining
		DType* nextData = NULL;
		// added loopCnt as a safety measure
		int loopCnt = 0;
		do
		{
			bcl = GetBaseClassList(opData->SymScope(), false, bailMonitor);
			if (bcl.GetLength())
			{
				nextData = FindSym(&opStr, NULL, &bcl);
				if (nextData)
				{
					if (nextData == opData)
						nextData = NULL;
					else
						opData = nextData;
				}
			}
			else
				break;
		} while (nextData && ++loopCnt < 5);
	}

	return opData;
}

bool MultiParse::HasPointerOverride(const WTString& sym)
{
	WTString ptrOpStr("->");
	WTString bcl(GetBaseClassList(sym, false, NULL));
	DType* opData = FindSym(&ptrOpStr, NULL, &bcl);
	return opData != NULL;
}

DTypePtr MultiParse::GetFileData(const CStringW& fileAndPath)
{
	DType* fileData = g_pGlobDic->GetFileData(fileAndPath);
	if (!fileData)
	{
		FileDic* sysDic = ::GetSysDic();
		fileData = sysDic->GetFileData(fileAndPath);
		if (!fileData)
		{
			// need to check the sysdic that corresponds to the file type,
			// don't just assume last used dic is appropriate.
			int ftype = ::GetFileType(fileAndPath);
			FileDic* sysDic2 = ::GetSysDic(ftype);
			if (sysDic != sysDic2)
				fileData = ::GetSysDic(ftype)->GetFileData(fileAndPath);
		}
	}

	DTypePtr fd;
	if (fileData)
	{
		fd = std::make_shared<DType>(fileData);
		return fd;
	}

	return fd;
}

void MultiParse::Tdef(const WTString& s, WTString& def, int AllDefs, int& type, int& attrs)
{
	_ASSERTE(0 == type && 0 == attrs);
	DEFTIMER(TDefTimer);
	// using unused arg as flag to qry all three dictionaries.
	// This is needed or else GetBaseClass will choke on
	// a forward declaration in a local header file.
	bool hasscope = s.c_str()[0] == DB_SEP_CHR; // could be file "c:..." where c: is not scope
	// find exact.
	WTString sym = StrGetSym(s);
	def.Empty();
	WTString scope = StrGetSymScope(s);
	if (!scope.GetLength())
		scope = GetGlobalNameSpaceString();
	FindData fds(hasscope ? &sym : &s, &DB_SEP_STR, hasscope ? &scope : NULL); // added ":" so we can find globals
	DB_READ_LOCK;

	// look in all three dictionaries
	// append all defs and return first type found

	if (m_pLocalDic->Find(&fds))
	{
		type = (int)fds.record->MaskedType();
		attrs = (int)fds.record->Attributes();
		def = fds.record->Def();
		fds.scoperank = -9999;
		fds.record = NULL;
		if (!AllDefs)
			return;
	}

	if (g_pGlobDic->Find(&fds))
	{
		if (!type)
		{
			type = (int)fds.record->MaskedType();
			attrs = (int)fds.record->Attributes();
		}

		WTString fdsRecordDef(fds.record->Def());
		if (def.Find(fdsRecordDef) == -1)
		{
			if (def.length())
				def = def + '\f' + fdsRecordDef;
			else
				def = fdsRecordDef;
		}
		fds.scoperank = -9999;
		fds.record = NULL;
		if (!AllDefs)
			return;
	}

	if (SDictionary()->Find(&fds))
	{
		if (!type)
		{
			type = (int)fds.record->MaskedType();
			attrs = (int)fds.record->Attributes();
		}

		WTString fdsRecordDef(fds.record->Def());
		if (def.Find(fdsRecordDef) == -1)
		{
			if (def.length())
				def = def + '\f' + fdsRecordDef;
			else
				def = fdsRecordDef;
		}
	}
}

WTString MultiParse::def(const WTString& sym)
{
	LOG("MultiParse::def");
	DB_READ_LOCK;
	WTString defStr;
	int type, attrs;
	type = attrs = 0;
	Tdef(sym, defStr, FALSE, type, attrs);
	return defStr;
}

const char* MultiParse::FormatDef(int type)
{
	DEFTIMER(FormatDefTimer);
	// process a definition striping spaces and comments...
	m_DefBuf[0] = '\0';
	if (!m_pDefBuf)
		m_pDefBuf = &m_DefBuf[0];
	const char* p = &m_p[m_cp - 1];
	char* bp = m_pDefBuf;
	int inTemplate = 0;
	bool hadcolon = false;
	int deep = 0;
	char lc = '\0';
	LPSTR tstart = 0;
	bool assignment = false;
	while (*p && (bp - m_pDefBuf + 5) < MAXLN)
	{
		char c = *p;
		switch (c)
		{
		case '\\': // leave '\' in def
			// only strip '\'s if '\<CRLF>'
			if (p[1] != '\r' && p[1] != '\n')
			{
				*bp++ = *p++;
				c = *p;
				break;
			}
			// strip '\<crlf>'
		case '\n':
			if (Is_Tag_Based(FileType()))
				goto done;
		case ' ':
		case 0xA0: // copy code via eudora turns spaces into 0xa0's
		case '\t':
			// strip white space
			while (p[1] == ' ' || p[1] == '\t' || p[1] == '\n' || p[1] == '\r')
			{
				p++;
				if (c != '\\' && (*p == '\r' || *p == '\n') && type == DEFINE)
					goto done;
				if (c == '\\' && *p == '\n')
					break;
			}
			// in case unix file #define foo<\n>
			if (c == '\n' && type == DEFINE)
				goto done;
			c = ' ';
			break;
		case '(':
		case '[':
			deep++;
			break;
		case '<':
			if (!assignment)
			{
				inTemplate++;
				tstart = bp;
			}
			break;
		case '>':
			if (!inTemplate && type != DEFINE && lc != '-') // not #define pfoo p->foo
				goto done;

			inTemplate--;
			if (!inTemplate && type == TEMPLATETYPE)
			{
				*bp++ = *p++;
				if (deep)
					return NULLSTR.c_str(); // somethings wrong, contains <(foo> isnt a template
				goto done;
			}
			if (!inTemplate)
			{ // go back and encode template
				*bp++ = c;
				if (tstart && type != DEFINE) // not #include <file.h>
					for (; tstart && tstart < bp; tstart++)
						*tstart = EncodeChar(*tstart);
				tstart = 0;
				c = '\0';
			}
			break;

		case ')':
		case ']':
			if (deep > 0)
				deep--;
			else
				goto done;
			break;
		case '/':
			c = '\0';
			if (p[1] == '/')
			{
				while (p[1] && p[1] != '\n')
					p++;
				// #define ... // stop after comment
				if (type == DEFINE)
					goto done;
			}
			else if (p[1] == '*')
			{
				while (p[0] && !(p[0] == '*' && p[1] == '/'))
					p++;
				if (*p)
					p++;
			}
			else
				c = '/';
			break;
			//
			// was comment break on #define  .... // ... . . ->  ->   ->
		case '\r':
			if (type == DEFINE)
				goto done;
			c = '\0';
			break;
		case '"':
		case '\'':

			while ((bp - m_pDefBuf) < MAXLN && p[1] && p[1] != c && p[1] != '\n')
			{
				if (p[1] == '\\')
					*bp++ = *p++;
				*bp++ = *p++;
			}
			*bp++ = *p++;
			break;
		case ',':
			if (deep || inTemplate)
				break;
			if (IS_OBJECT_TYPE((uint)type) && hadcolon) // class foo : bar, baz
				break;
		case '{':
		case '}':
		case ';':
			if (type == DEFINE)
				break;
			goto done;
		/////////////////
		// test for template
		// look for "foo<bar>" NOT:
		// "foo<bar->baz" or <foo&& bar > xxx
		case '&':
			if (p[1] != '&')
				break;
		case '-':
		// case '|': // allow CWinTraits<WS_OVERLAPPEDWINDOW | WS_VISIBLE>
		case '^':
			if (type == TEMPLATETYPE)
				goto done;
			break;
		case ':':
			hadcolon = true;
			break;
		case '=':
			assignment = true;
			break;
		case '#':
			// #ifdef or something? see CCtrlView class def
			if (type != DEFINE)
				if (!deep) // allow x11 style methods int foo(#if args\n int args;\n#endif\n);
					goto done;
		}
		if (c)
			*bp++ = c;
		lc = c;
		p++;
	}

	// def truncated...
	{
		// see case=25876 for a problem this causes (see MultiParse::AddPPLn)
		for (int x = 3; x && (bp - m_pDefBuf) < MAXLN; x--)
			*bp++ = '.';
	}
done:
	// strip all DB_FIELD_DELIMITERs from def
	{
		const LPCTSTR pStop = m_pDefBuf + MAXLN;
		*bp = '\0';
		for (char* pc = m_pDefBuf; *pc && pc < bp && pc < pStop; pc++)
			if (*pc == DB_FIELD_DELIMITER)
				*pc = ' ';
		if (m_parseType == ParseType_GotoDefs)
		{ // Looks good, but needs testing...
			// use add ";" or "{...}" to def. Differentiate between dec/def's for goto def
			if ((bp - m_pDefBuf + 6) < MAXLN && (!type || IS_OBJECT_TYPE((uint)type)))
			{
				if (*p == '{')
					strcat(m_pDefBuf, " {...}");
				else if (*p == ';' || *p == ',')
					strcat(m_pDefBuf, ";");
			}
		}
	}
	return m_pDefBuf;
}

bool MultiParse::IsPointer(const WTString& symdef, int deep)
{
	// Needed cause v_pointer only means that a * was used in the def,
	// it misses LPSTR str;
	if (deep > 5)
		return false;
	// Strip all but bare type of symbol;
	static const WTString kChars = "*^&<({[=" + ::EncodeScope("*^");
	const int p = symdef.find_first_of(kChars);
	token2 t = (p == NPOS) ? symdef : symdef.Left(p);
	t.ReplaceAll(" :: ", "::"); // allow for "foo::member var" -Jer

	bool ptr = false;
	int maxtries = 5;
	WTString def;
	while (maxtries-- && !ptr && t.more())
	{
		static const WTString kReadChars = "\t *&^" + ::EncodeScope(" *^"); // removed ';' so we did not look for :member in "foo::member var" -Jer // t.read("\t :*&");
		WTString s = t.read(kReadChars);
		// foo<char*> f; f.xxx don't change '.' to '->'
		//		if(s.Find(EncodeChar('*'))!= NPOS)
		//			return(true);
		if (s.length())
		{
			int type, attrs;
			type = attrs = 0;
			Tdef(DB_SEP_STR + s, def, 0, type, attrs);
			if (attrs & V_POINTER && (IS_OBJECT_TYPE((uint)type) || type == DEFINE))
			{
				vLog("PtrOp: IsPointer V_POINTER s(%s) d(%s) t(%d)", s.c_str(), def.c_str(), type);
				ptr = true;
			}
			else if ((IS_OBJECT_TYPE((uint)type) || type == TYPE || type == DEFINE) && def != symdef && t.more())
			{
				ptr = IsPointer(def, deep + 1);
				vLog("PtrOp: IsPointer recurse v(%d) sd(%s) d(%s) t(%d)", ptr, symdef.c_str(), def.c_str(), type);
			}
		}
	}
	return ptr;
}

void MultiParse::ForEach(DTypeForEachCallback fn, bool& stop, bool searchSys /*= true*/)
{
	m_pLocalDic->ForEach(fn, stop);
	if (!stop && (FileType() == Src || FileType() == UC))
		m_pLocalHcbDic->ForEach(fn, stop);
	if (!stop)
		g_pGlobDic->ForEach(fn, stop);
	if (!stop && searchSys)
		SDictionary()->ForEach(fn, stop);
}

void MultiParse::ForEach(DTypeForEachCallback fn, bool& stop, DTypeDbScope dbScope)
{
	_ASSERTE(dbScope != DTypeDbScope::dbSystemIfNoSln);

	if (!stop && (DWORD)dbScope & (DWORD)DTypeDbScope::dbLocal)
	{
		m_pLocalDic->ForEach(fn, stop);

		if (!stop && (FileType() == Src || FileType() == UC))
			m_pLocalHcbDic->ForEach(fn, stop);
	}
	if (!stop && (DWORD)dbScope & (DWORD)DTypeDbScope::dbSolution)
		g_pGlobDic->ForEach(fn, stop);
	if (!stop && (DWORD)dbScope & (DWORD)DTypeDbScope::dbSystem)
		SDictionary()->ForEach(fn, stop);
}

DType* MultiParse::FindAnySym(const WTString& sym)
{
	if (g_CodeGraphWithOutVA)
		return NULL;
	DType* guess = GetDFileMP(FileType())->m_pLocalDic->FindAnySym(sym);
	if (!guess)
		guess = m_pLocalDic->FindAnySym(sym);
	if (!guess)
		guess = g_pGlobDic->FindAnySym(sym);
	if (!guess)
		guess = SDictionary()->FindAnySym(sym);
	if (guess)
		guess->LoadStrs();
	return guess;
}

// Iterate all defs looking for the best color type
class FindBestColorTypeClass : public DB_Iterator
{
	DefObj* m_BestGuess;
	DefObj* m_AnyGuess;
	uint mSymHash2;

  public:
	FindBestColorTypeClass()
	{
		g_DBFiles.SetOkToIgnoreGetSymStrs(TRUE);
	}

	~FindBestColorTypeClass()
	{
		g_DBFiles.SetOkToIgnoreGetSymStrs(FALSE);
	}

	DefObj* FindBestColorType(MultiParsePtr mp, const WTString& sym)
	{
		m_BestGuess = m_AnyGuess = nullptr;
		mSymHash2 = DType::GetSymHash2(sym.c_str());
		Iterate(mp, sym);
		// #ifdef _DEBUG
		//		if(!m_BestGuess && !m_AnyGuess)
		//			_asm nop; // for setting a breakpoint
		// #endif // _DEBUG
		return m_BestGuess ? m_BestGuess : m_AnyGuess;
	}
	virtual void OnSym(DefObj* obj)
	{
		// Verify symbol match
		if (obj->SymHash() != m_hashID)
			return;

		if (!obj->SymMatch(mSymHash2))
			return;

		if (m_DB_ID == MultiParse::DB_VA)
		{
			if (obj->MaskedType() == FUNC)
			{
				WTString objDef(obj->Def());
				if (objDef.Find("alias ") == 0)
				{
					// Hack for C#/VB aliases, Case 15286 #aliasHack
					// In the .va files, they are defined as VAR's, but they need to be colored as their alias
					WTString alias = DBColonToSepStr(TokenGetField(objDef.Mid(6), " ").c_str()); // strip "alias "
					DefObj* o = (DefObj*)m_mp->FindExact(alias);
					if (o)
						obj = o;
				}
			}
			else if (obj->MaskedType() == TYPE && m_sym == "value")
			{
				// [case: 37888] hack for coloring of "value"
				m_AnyGuess = obj;
				return;
			}
		}
		else if (m_DB_ID == MultiParse::DB_LOCAL)
		{
			if (!m_BestGuess)
			{
				const WTString curSym(obj->Sym());
				// class members should not be bold, Case 14574
				DefObj* o = (DefObj*)g_pGlobDic->FindExact(curSym, obj->ScopeHash(), FALSE);
				if (!o)
					o = (DefObj*)m_mp->SDictionary()->FindExact(curSym, obj->ScopeHash(), FALSE);
				if (o)
					obj = o;
			}
		}

		const uint curType = (uint)obj->MaskedType();
		if (NUMBER == curType)
			return; // Case 15040

		m_AnyGuess = obj;

		if (NAMESPACE == curType && !obj->IsDbNet() && MultiParse::DB_SYSTEM != m_DB_ID &&
		    gShellAttr->IsDevenv11OrHigher())
		{
			static const WTString kWindowsNs = DB_SEP_STR + "Windows";
			static const WTString kPlatformNs = DB_SEP_STR + "Platform";
			const WTString objSymScope(obj->SymScope());
			if (0 == objSymScope.Find(kWindowsNs))
			{
				static const WTString kWindowsNs2 = kWindowsNs + DB_SEP_STR;
				if (objSymScope == kWindowsNs || 0 == objSymScope.Find(kWindowsNs + DB_SEP_STR))
				{
					// [case: 67036] don't use obj as best guess
					// if sym exists in DB_SYSTEM, use that for italics feature
					return;
				}
			}
			else if (0 == objSymScope.Find(kPlatformNs))
			{
				static const WTString kPlatformNs2 = kPlatformNs + DB_SEP_STR;
				if (objSymScope == kPlatformNs || 0 == objSymScope.Find(kPlatformNs + DB_SEP_STR))
				{
					// [case: 67036] don't use obj as best guess
					// if sym exists in DB_SYSTEM, use that for italics feature
					return;
				}
			}
		}

		if (!obj->IsConstructor() && curType != GOTODEF)
		{
			if (!obj->IsVaForwardDeclare())
			{
				// case: 19427 using statements are not best guesses
				// consider using an attribute for "using" to prevent string operation here
				if (!obj->DefStartsWith("using"))
				{
					m_BestGuess = obj;
					if (DEFINE == curType ||
					    IS_OBJECT_TYPE(curType)) // Prefer types and defines for coloring: case 15959
						Break();
				}
			}
		}
	}
	virtual void OnNextDB(int id)
	{
		if (m_BestGuess != NULL)
			Break();
	}
};

DTypePtr MultiParse::FindBestColorType(const WTString& sym)
{
	FindBestColorTypeClass it;
	DType* ret = it.FindBestColorType(shared_from_this(), sym);
	if (ret)
	{
		// [case: 140758]
		// override for common redefinitions that result in unexpected coloring and/or italicization
		switch (sym[0])
		{
		case 'f':
			// foreach
			if (sym.GetLength() == 7 && sym == "foreach")
				return std::make_shared<DType>("foreach", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
			break;
		case 'n':
			// new
			if (ret->type() == DEFINE && sym.GetLength() == 3 && sym[1] == 'e' && sym[2] == 'w')
			{
				return std::make_shared<DType>("", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
			}
			break;
		case 's':
			// std
			if (IsCFile(gTypingDevLang) && sym.GetLength() == 3 && sym[1] == 't' && sym[2] == 'd')
			{
				if (ret->type() == DEFINE || (ret->type() == NAMESPACE && !ret->IsSysLib()))
					return std::make_shared<DType>("std", "", (uint)NAMESPACE, uint(V_VA_STDAFX | V_SYSLIB),
					                               (uint)VA_DB_Cpp);
			}
			break;
		case 'S':
			// System
			if (sym[1] == 'y' && sym == "System")
			{
				if (CS == gTypingDevLang || (IsCFile(gTypingDevLang) && GlobalProject->CppUsesClr()))
					if (ret->type() == STRUCT || (ret->type() == NAMESPACE && !ret->IsSysLib()))
						return std::make_shared<DType>("System", "", (uint)NAMESPACE, uint(V_VA_STDAFX | V_SYSLIB),
						                               (uint)VA_DB_Net);
			}
			break;
		}

		return std::make_shared<DType>(ret);
	}

	// attempt to color specific words without impacting dbs for keyword extensions
	_ASSERTE(!sym.IsEmpty());
	switch (sym[0])
	{
	case 'd':
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (sym == "default")
			return std::make_shared<DType>("default", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
#endif
		break;
	case 'f':
		if (sym.GetLength() == 7 && sym == "foreach")
			return std::make_shared<DType>("foreach", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
		break;
	case 'i':
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (sym == "implements")
			return std::make_shared<DType>("implements", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
		if (sym == "index")
			return std::make_shared<DType>("index", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
#endif
		break;
	case 'n':
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (sym == "nodefault")
			return std::make_shared<DType>("nodefault", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
#endif
		break;
	case 'p':
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (sym == "published")
			return std::make_shared<DType>("published", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
#endif
		break;
	case 'r':
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (sym == "read")
			return std::make_shared<DType>("read", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
#endif
		break;
	case 's':
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (sym == "stored")
			return std::make_shared<DType>("stored", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
#endif
		break;
	case 'w':
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
		if (sym == "write")
			return std::make_shared<DType>("write", "", (uint)RESWORD, (uint)V_VA_STDAFX, (uint)VA_DB_Cpp);
#endif
		break;
	}

	return nullptr;
}

void MultiParse::FindAllReferences(const CStringW& project, const CStringW& file, FindReferences* ref,
                                   WTString *txt /* = NULL */, volatile const INT* monitorForQuit /*= NULL*/,
                                   bool fullscope /*= true*/)
{
	Init();
	CLEARERRNO;
	WTString code_rf;
	if(!txt)
		code_rf = ReadFile(file);
	WTString &code = txt ? *txt : code_rf;

	if (code.GetLength() > 1 && code.c_str()[0] & 0x80) // unicode files
	{
		// added in change 5034
		// not likely used anymore since we toss BOM on file load
		code.remove(0, 3);
	}

	if (code.Find('\n') == -1)
		code.ReplaceAll('\r', '\n'); // mac files

	m_parseAll = true;
	if (CAN_USE_NEW_SCOPE() || !g_currentEdCnt || CAN_USE_NEW_SCOPE(FileType()))
	{
		VAParseMPFindUsageFunc(project, file, shared_from_this(), ref, code, monitorForQuit, fullscope);
	}
	else
	{
		ASSERT(FALSE);
	}
}

int MultiParse::FindExactList(LPCSTR sym, DTypeList& cdlist, bool searchSys /*= true*/, bool searchProj /*= true*/)
{
	uint scopeId = WTHashKey(StrGetSymScope(sym));
	int found = m_pLocalDic->FindExactList(sym, scopeId, cdlist);
	if (!found && (FileType() == Src || FileType() == UC))
		found += m_pLocalHcbDic->FindExactList(sym, scopeId, cdlist);
	if (searchProj)
		found += g_pGlobDic->FindExactList(sym, scopeId, cdlist);
	if (searchSys)
		found += SDictionary()->FindExactList(sym, scopeId, cdlist);
	return found;
}

int MultiParse::FindExactList(uint hashVal, uint scopeId, DTypeList& cdlist, bool searchSys /*= true*/,
                              bool searchProj /*= true*/)
{
	int found = m_pLocalDic->FindExactList(hashVal, scopeId, cdlist);
	if (!found && (FileType() == Src || FileType() == UC))
		found += m_pLocalHcbDic->FindExactList(hashVal, scopeId, cdlist);
	if (searchProj)
		found += g_pGlobDic->FindExactList(hashVal, scopeId, cdlist);
	if (searchSys)
		found += SDictionary()->FindExactList(hashVal, scopeId, cdlist);
	return found;
}

// introduced in change 5309 "Added FindExact2 because FindExact actually FindsBestGuess."
DType* MultiParse::FindExact2(LPCSTR sym, bool concatDefs /*= true*/, int _searchForType /*= 0*/,
                              bool honorHideAttr /*= true*/)
{
	uint searchForType = (uint)_searchForType;
	_ASSERTE(searchForType != CachedBaseclassList); // use FindCachedBcl instead
	if (!sym)
		return NULL;
	uint scopeId;
	WTString symStr;
	if (sym[0] == DB_SEP_CHR)
	{
		symStr = StrGetSym(sym);
		scopeId = WTHashKey(StrGetSymScope(sym));
	}
	else
	{
		symStr = sym;
		scopeId = 0;
	}
	DType* data = GetDFileMP(FileType())->m_pLocalDic->FindExact(symStr, scopeId, searchForType, honorHideAttr);

	if (!data)
		data = g_pGlobDic->FindExact(symStr, scopeId, concatDefs, searchForType, honorHideAttr);
	if (!data && (FileType() == Src || FileType() == UC))
		data = m_pLocalHcbDic->FindExact(symStr, scopeId, concatDefs, searchForType, honorHideAttr);
	if (!data)
		data = m_pLocalDic->FindExact(symStr, scopeId, searchForType, honorHideAttr);
	if (!data)
		data = SDictionary()->FindExact(symStr, scopeId, concatDefs, searchForType, honorHideAttr);
	return data;
}

DType* MultiParse::FindCachedBcl(const WTString& bclKey)
{
	_ASSERTE(!bclKey.IsEmpty());
	DType* data = g_pGlobDic->FindExact(bclKey, 0, false, CachedBaseclassList, false);
	if (!data)
	{
		if (FileType() == Src || FileType() == UC)
			data = m_pLocalHcbDic->FindExact(bclKey, 0, false, CachedBaseclassList, false);
		else
			data = m_pLocalDic->FindExact(bclKey, 0, CachedBaseclassList, false);
		_ASSERTE(!data || (data->infile() && !data->inproject()));
	}
	else
		_ASSERTE(!data->infile() && data->inproject());

	return data;
}

DType* MultiParse::FindMatch(DTypePtr symDat)
{
	if (!symDat)
		return nullptr;

	DType* data = nullptr;
	if (!data && (symDat->IsDbSolution() || symDat->IsDbExternalOther()))
		data = g_pGlobDic->FindMatch(symDat);
	if (!data && symDat->IsSysLib())
		data = SDictionary()->FindMatch(symDat);
	if (!data && (FileType() == Src || FileType() == UC))
		data = m_pLocalHcbDic->FindMatch(symDat);
	if (!data)
		data = m_pLocalDic->FindMatch(symDat);
	return data;
}

void MultiParse::ImmediateDBOut(const WTString& sym, const WTString& def, uint type, uint attrs, int line)
{
	_ASSERTE((type & TYPEMASK) == type);
	const uint fileID = m_mp ? m_mp->m_fileId : m_fileId;
	_ASSERTE(0 < m_ftype && kLanguageFiletypeCount > m_ftype);
	_ASSERTE(m_writeToDFile);
	_ASSERTE(vaIncludeBy != type && vaIncludeBy != type);
	_ASSERTE(vaInheritsFrom != type && vaInheritedBy != type);

	// [case: 65910]
	if (mFormatAttrs & V_VA_STDAFX)
		attrs |= V_VA_STDAFX;

	std::shared_ptr<DbFpWriter> dbw(g_DBFiles.GetDBWriter(m_fileId, m_VADbId, TRUE));
	_ASSERTE(dbw);
	dbw->DBOut(sym, def, type, attrs, line, fileID);
}

void MultiParse::DBOut(const WTString& sym, const WTString& def, uint type, uint attrs, int line)
{
	_ASSERTE((type & TYPEMASK) == type);
	const uint fileID = m_mp ? m_mp->m_fileId : m_fileId;
	_ASSERTE(0 < m_ftype && kLanguageFiletypeCount > m_ftype);
	_ASSERTE(m_writeToDFile);
	_ASSERTE(vaInheritedBy != type);

	// [case: 65910]
	if (mFormatAttrs & V_VA_STDAFX)
		attrs |= V_VA_STDAFX;

	if (TEMPLATE == type)
		ImmediateDBOut(sym, def, type, attrs, line);
	else if (type == vaInclude)
	{
		mDbOutIncludesCache.push_back(DbOutParseData(sym, def, type, attrs, line, fileID));

		// vaInclude also hits normal DBOut because that is used to ensure
		// file is parsed on load of idx
		mDbOutCache.push_back(DbOutParseData(sym, def, type, attrs, line, fileID));
	}
	else if (vaIncludeBy == type)
		mDbOutIncludeByCache.push_back(DbOutParseData(sym, def, type, attrs, line, fileID));
	else if (vaInheritsFrom == type)
	{
		if (ParseType_Locals != m_parseType)
			mDbOutInheritsFromCache.push_back(DbOutParseData(sym, def, type, attrs, line, fileID));
	}
	else
	{
		mDbOutCache.push_back(DbOutParseData(sym, def, type, attrs, line, fileID));
		if (mDbOutCache.size() > 2000)
		{
			_ASSERTE(m_writeToDFile);
#if !defined(SEAN)
			try
#endif // !SEAN
			{
				std::shared_ptr<DbFpWriter> dbw(g_DBFiles.GetDBWriter(m_fileId, m_VADbId, TRUE));
				_ASSERTE(dbw);
				for (const auto& dat : mDbOutCache)
					dbw->DBOut(dat.mSym, dat.mDef, dat.mType, dat.mAttrs, dat.mLine, dat.mFileId);
			}
#if !defined(SEAN)
			catch (...)
			{
				VALOGEXCEPTION("MP:FDBC:");
				_ASSERTE(!"Exception in FDBOut");
			}
#endif // !SEAN
			mDbOutCache.clear();
		}
	}
}

void MultiParse::FlushDbOutCache()
{
	_ASSERTE(m_writeToDFile);
	IncludesDb::StoreIncludeData(mDbOutIncludesCache, m_VADbId, vaInclude);

#if !defined(SEAN)
	try
#endif // !SEAN
	{
		std::shared_ptr<DbFpWriter> dbw(g_DBFiles.GetDBWriter(m_fileId, m_VADbId, TRUE));
		_ASSERTE(dbw);
		for (const auto& dat : mDbOutCache)
			dbw->DBOut(dat.mSym, dat.mDef, dat.mType, dat.mAttrs, dat.mLine, dat.mFileId);
	}
#if !defined(SEAN)
	catch (...)
	{
		VALOGEXCEPTION("MP:FDBC:");
		_ASSERTE(!"Exception in FDBOut");
	}
#endif // !SEAN
	mDbOutIncludesCache.clear();
	mDbOutCache.clear();

	IncludesDb::StoreIncludeData(mDbOutIncludeByCache, m_VADbId, vaIncludeBy);
	mDbOutIncludeByCache.clear();

	InheritanceDb::StoreInheritanceData(mDbOutInheritsFromCache, m_VADbId, m_ftype, vaInheritsFrom, mSolutionHash);
	mDbOutInheritsFromCache.clear();
}

void MultiParse::OpenIdxOut(const CStringW& file, uint dbFlags)
{
	mSolutionHash = GlobalProject->GetSolutionHash();
	_ASSERTE(!m_writeToDFile);
	_ASSERTE(0 < m_ftype && kLanguageFiletypeCount > m_ftype);
	// Open letter files for each dict
	m_fileId = gFileIdManager->GetFileId(file);
	if (m_VADbId == DbTypeId_Error)
		m_VADbId = g_DBFiles.DBIdFromParams(FileType(), m_fileId, m_isSysFile, m_parseAll,
		                                    mIsVaReservedWordFile ? VA_DB_ExternalOther : dbFlags);
	_ASSERTE(mDbOutCache.empty() && "Someone didn't flush cache before OpenIdxOut");
	_ASSERTE(mDbOutIncludesCache.empty() && "Someone didn't flush cache before OpenIdxOut");
	_ASSERTE(mDbOutIncludeByCache.empty() && "Someone didn't flush cache before OpenIdxOut");
	_ASSERTE(mDbOutInheritsFromCache.empty() && "Someone didn't flush cache before OpenIdxOut");
	std::shared_ptr<DbFpWriter> dbw(g_DBFiles.GetDBWriter(m_fileId, m_VADbId, FALSE));
	m_writeToDFile = TRUE;

	mDbOutMacroProjCnt = mDbOutMacroSysCnt = mDbOutMacroLocalCnt = 0;
}

void MultiParse::CloseIdxOut()
{
	_ASSERTE(mDbOutCache.empty() && "Someone didn't flush cache before CloseIdxOut");
	_ASSERTE(mDbOutIncludesCache.empty() && "Someone didn't flush cache before CloseIdxOut");
	_ASSERTE(mDbOutIncludeByCache.empty() && "Someone didn't flush cache before CloseIdxOut");
	_ASSERTE(mDbOutInheritsFromCache.empty() && "Someone didn't flush cache before CloseIdxOut");
	m_writeToDFile = FALSE;
}

void MultiParse::ClearTemporaryMacroDefs()
{
	if ((mDbOutMacroLocalCnt + mDbOutMacroSysCnt + mDbOutMacroProjCnt) > 20)
	{
		m_pLocalDic->RemoveVaMacros();
		mDbOutMacroLocalCnt = mDbOutMacroSysCnt = mDbOutMacroProjCnt = 0;
	}
}

int MultiParse::ReadIdxData(LPCVOID data, DWORD sz, BOOL checkIncludes)
{
	bool modifiedNsList = false;

	int count = (int)(sz / sizeof_db_entry);
	int includeCount = 0;
	ASSERT(sz == (count * sizeof_db_entry));

	DEFTIMER(ReadIdxFile2);
	FileDic* sysDic = nullptr;
	int lastSysDicType = -1;
	uint lastCheckedSysFid = 0;
	BOOL lastCheckedSysFidResult = true;
	const bool loadNetDb = !GlobalProject || GlobalProject->ShouldLoadNetDb();
//	char buffer[sizeof(DType)] = {0};
	for (int i = 0; i < count; i++)
	{
//		memcpy(buffer, (const char*)data + sizeof_db_entry * i, sizeof_reduced_DType);
//		const DType& _dtype = *(DType*)buffer;
		const DType& _dtype = *(DType*)((const char*)data + sizeof_db_entry * i);

		const uint attrs = _dtype.Attributes();
		const uint dbFlags = _dtype.DbFlags();
		_ASSERTE(_dtype.type() != vaIncludeBy);
		//		_ASSERTE(!_dtype.HasData());

		if (dbFlags & VA_DB_Cpp)
		{
			// [case: 97190] sparseSysLoad can cause missing system symbols when
			// using multiple versions of win sdks.  For example, a file includes
			// "foo.h".  It gets resolved for example to the 7.1 sdk dir when solution
			// A is loaded.  But in another solution B, the 7.1 sdk dir is not a valid
			// include dir.  "foo.h" in solution B resolves to the 8.0 sdk dir.
			// In that solution, foo.h is not parsed and if Psettings->mSparseSysLoad
			// is enabled, then symbols in foo.h are not known (since include
			// resolution is stored in the db).
			// Switching back to solution A, symbols in foo.h work.
			// Psettings->mSparseSysLoad now defaults to off.

			// [case: 92495] VA_DB_SolutionPrivateSystem are by definition included by solution
			if (!(dbFlags & VA_DB_SolutionPrivateSystem) && Psettings->mSparseSysLoad)
			{
				const uint fid = _dtype.FileId();
				if (fid != lastCheckedSysFid)
				{
					if (m_isSysFile)
					{
						// here because sys file is actively being parsed
						lastCheckedSysFidResult = true;
					}
					else
					{
						// loading indexes from dir, or actively being parsed but not m_isSysFile
						const CStringW sysFile(gFileIdManager->GetFile(fid));
						lastCheckedSysFidResult = IncludeDirs::IsSystemFile(sysFile);
						if (!lastCheckedSysFidResult)
						{
							if (-1 != sysFile.Find(L"stdafx"))
							{
								// always load va files
								CStringW pth(VaDirs::GetDllDir() + L"misc\\");
								if (::_wcsnicmp(pth, sysFile, (uint)pth.GetLength()) == 0)
								{
									lastCheckedSysFidResult = true;
								}
								else
								{
									pth = VaDirs::GetUserDir() + L"misc\\";
									if (::_wcsnicmp(pth, sysFile, (uint)pth.GetLength()) == 0)
										lastCheckedSysFidResult = true;
								}
							}
							else
							{
								const CStringW ext(::GetBaseNameExt(sysFile));
								if (ext.CompareNoCase(L"tlh") == 0)
								{
									const CStringW tempDir(::GetTempDir());
									if (::_wcsnicmp(tempDir, sysFile, (uint)tempDir.GetLength()) == 0)
									{
										// allow tlh files from temp dir; see:#parseTempTlhAsGlobal
										lastCheckedSysFidResult = true;
									}
								}
							}
						}

						if (!lastCheckedSysFidResult)
							vCatLog("Parser.MultiParse", "MP::RID skip idx load -- not sys inc '%s'",
							     (LPCTSTR)CString(sysFile)); // Psettings->mSparseSysLoad note
					}

					lastCheckedSysFid = fid;
				}

				if (!lastCheckedSysFidResult)
				{
					// do not load symbols from include dirs that are not appropriate
					// for current solution
					continue;
				}
			}
		}
		else if (dbFlags & VA_DB_Net)
		{
			if (!loadNetDb && Psettings->mSparseSysLoad)
			{
				// don't load if solution is only C/C++ and doesn't have net/clr
				continue;
			}
		}

		DType sd(_dtype.ScopeHash(), _dtype.SymHash(), _dtype.SymHash2(), nullptr, nullptr, _dtype.type(),
		         _dtype.Attributes(), _dtype.DbFlags(), _dtype.Line(), _dtype.FileId(), _dtype.GetDbOffset(),
		         (int)_dtype.GetDbSplit());

		if (attrs & V_IDX_FLAG_INCLUDE)
		{
			const uint curType = sd.MaskedType();
			const WTString fileIdStr = sd.Def();
			const CStringW file = gFileIdManager->GetFile(fileIdStr);

			if (!file.IsEmpty())
			{
				if (vaInclude == curType)
				{
					// [case: 97154]
					// always do the quick include check (ignore checkIncludes param for simple IsIncluded)
					_ASSERTE(::StartsWith(sd.SymScope(), "+ic:", FALSE));
					if (!IsIncluded(file))
					{
						if (checkIncludes)
						{
							// synchronous/interleaved parse
							ParseGlob((LPVOID)(LPCWSTR)file);
						}
						else
						{
							// queue for parse so that we can reeval after directory load is complete
							AutoLockCs l(mPendingParseLock);
							mPendingIncludes.AddUniqueNoCase(file);
						}
					}
					else if (checkIncludes && includeCount++ < 100 && !IsIncluded(file, TRUE))
					{
						// case=6562
						//		limit number of includes checked by time, due to file access slowdown
						// case=2652
						//		external headers are not checked for mod during project load
						g_ParserThread->QueueFile(file);
						--includeCount; // don't count this include if there was a time mismatch
					}
				}
				else if (checkIncludes && UNDEF == curType)
				{
					// "+using" is only expected when reading in idx files for c++/cli
					_ASSERTE(sd.SymScope() == "+using");
					_ASSERTE(g_pMFCDic);
					_ASSERTE(GlobalProject && (GlobalProject->CppUsesClr() || GlobalProject->CppUsesWinRT()));
					NetImportDll(file);
					GetSysDic(Src);
					g_pGlobDic->add(std::move(sd));
				}
			}

			if (checkIncludes || vaInclude == curType)
				continue;
		}

		if (attrs & V_IDX_FLAG_USING)
		{
			const WTString nsScp(sd.Scope());
			const WTString ns(sd.Def());
			const CStringW sdFile(gFileIdManager->GetFile(sd.FileId()));
			const int langTypeOfUsing(::GetFileType(sdFile));
			// when LoadIdxesInDir is run, m_ftype is meaningless - so use filetype of cur DType
			TempAssign<int> fType(m_ftype, langTypeOfUsing);
			if (Src == m_ftype && nsScp.GetLength() == 0)
			{
				// [case: 142375]
				// prepend ns to source file list of file-level namespace usings
				WTString namespaceList = ns + '\f';
				if (!m_namespaces.IsEmpty())
				{
					token2 t(m_namespaces);
					WTString s;
					for (int i2 = 0; i2 < 50 && t.more(); i2++)
					{
						t.read('\f', s);
						if (s.GetLength() && s != ns)
						{
							namespaceList += s;
							namespaceList += '\f';
						}
					}
				}

				m_namespaces = namespaceList;
			}
			else if (nsScp.GetLength() == 0 || !IsCFile(m_ftype))
			{
				if (!modifiedNsList)
				{
					modifiedNsList = true;
					if (g_loggingEnabled)
					{
						WTString nsStr(GetGlobalNameSpaceString());
						nsStr.ReplaceAll("\f", ";");
						vCatLog("Parser.MultiParse", "MP::RID: old ns: %s", nsStr.c_str());
					}
				}

				// prepend ns to NameSpaceString
				token2 t(GetGlobalNameSpaceString());
				WTString namespaceList = ns + '\f';
				const int kMaxNamespaces = 50;
				int i3 = 0;
				WTString s;
				for (; i3 < kMaxNamespaces && t.more(); i3++)
				{
					t.read('\f', s);
					if (s.GetLength() && s != ns)
					{
						namespaceList += s;
						namespaceList += '\f';
					}
				}

				if (i3 >= kMaxNamespaces)
				{
					vLogUnfiltered("WARN: MP::RID: truncated namespace list");
				}

				SetGlobalNameSpaceString(namespaceList);
			}
			else
			{
				AddNamespaceString(nsScp, ns);
			}
		}

		if ((dbFlags & VA_DB_LocalsParseAll) || mIsVaReservedWordFile)
		{
			_ASSERTE(sd.MaskedType() != vaInheritsFrom);
			if (FileType() == Src || FileType() == UC)
			{
				m_pLocalDic->add(DType(sd));
				m_pLocalHcbDic->add(std::move(sd));
			}
			else
				m_pLocalDic->add(std::move(sd));
		}
		else if (dbFlags & VA_DB_Net || dbFlags & VA_DB_Cpp)
		{
			if (dbFlags & VA_DB_Net)
			{
				if (lastSysDicType != CS)
				{
					_ASSERTE(lastSysDicType == -1);
					lastSysDicType = CS;
					sysDic = GetSysDic(lastSysDicType);
				}
			}
			else if (dbFlags & VA_DB_Cpp)
			{
				if (lastSysDicType != Src)
				{
					_ASSERTE(lastSysDicType == -1);
					lastSysDicType = Src;
					sysDic = GetSysDic(lastSysDicType);
				}
			}

			if (sysDic)
				sysDic->add(std::move(sd));
			else
				_ASSERTE(!"ReadIdxData doesn't know what sysDic to write to");
		}
		else
			g_pGlobDic->add(std::move(sd));
	}

	if (g_loggingEnabled && modifiedNsList)
	{
		WTString nsStr(GetGlobalNameSpaceString());
		nsStr.ReplaceAll("\f", ";");
		vCatLog("Parser.MultiParse", "MP::RID: new ns: %s", nsStr.c_str());
	}

	return count;
}

WTString MultiParse::GetGuessedBaseClass(const WTString& bc)
{
	WTString CachedBCLStr = WTString("**") + StrGetSym(bc);
	DType* bclData = FindExact(CachedBCLStr);
	if (bclData)
		return bclData->Def() + DB_SEP_STR;
	return NULLSTR;
}

DType* MultiParse::GuessAtBaseClass(WTString& sym, const WTString& bc)
{
	// Guess at BCL
	WTString CachedBCLStr = WTString("**") + StrGetSym(bc);
	DType* bclData = FindExact(CachedBCLStr);
	DType* data = NULL;
	if (bclData)
	{
		WTString def(bclData->Def());
		data = FindSym(&sym, NULL, &def);
	}

	if (!data)
	{
		data = FindAnySym(sym);
		if (data)
		{
			WTString memberScope = data->Scope();
			WTString bcl = memberScope + "\f"; //  + GetBaseClassList(memberScope);
			// Store in BCL cache; use V_INFILE instead of V_INPROJECT so that BCL class knows DType comes from DLoc
			add(CachedBCLStr, bcl, DLoc, CachedBaseclassList, V_INFILE | V_HIDEFROMUSER);
		}
	}
	return data;
}

FileDic* MultiParse::SDictionary(int ftype)
{
	_ASSERTE(0 < ftype && kLanguageFiletypeCount > ftype);
	return GetSysDic(ftype);
}

void MultiParse::SetBuf(LPCSTR buf, ULONG bufLen, ULONG cp, ULONG curLine)
{
	// added lock due to crash in MultiParse::ReadTo when two MPGetScopeCls
	// threads were active in VAParseMPMacroC::OnDirective - using the same
	// MultiParse from g_currentEdCnt.
	// first one called ClearBuf after second one called SetBuf.
	// Calls to SetBuf MUST always be paired with ClearBuf.
	mBufLock.Lock();
	::InterlockedIncrement(&mBufSetCnt);
	m_p = buf;
	m_len = (int)bufLen;
	m_cp = (int)cp;
	m_line = (int)curLine;
}

void MultiParse::ClearBuf()
{
	m_p = m_pDefBuf = NULL;
	m_line = m_len = m_cp = 0;
	if (mBufSetCnt)
	{
		::InterlockedDecrement(&mBufSetCnt);
		mBufLock.Unlock();
	}
}

void MultiParse::ClearCwData()
{
	ScopeInfo::ClearCwData();
	mUpdatedXref.reset();
}

DType MultiParse::FindBetterDtype(DType* pDat)
{
	_ASSERTE(pDat);
	DTypeList lst;
	const bool searchSys = !!pDat->IsSysLib();
	FindExactList(pDat->SymHash(), pDat->ScopeHash(), lst, searchSys);
	lst.FilterNonActiveProjectDefs();
	if (searchSys)
		lst.FilterNonActiveSystemDefs();
	if (!lst.size())
		return *pDat;

	// refresh and undo def concats
	lst.ReloadStrs();
	return lst.front();
}

void MultiParse::RemoveAllDefs(const CStringW& file, DTypeDbScope dbScp, bool singlePass /*= true*/)
{
	if (singlePass)
	{
		if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
			g_pGlobDic->RemoveAllDefsFrom_SinglePass(file);
		if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem)
			SDictionary()->RemoveAllDefsFrom_SinglePass(file);
	}
	else
	{
		if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
			g_pGlobDic->RemoveAllDefsFrom_InitialPass(file);
		if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem)
			SDictionary()->RemoveAllDefsFrom_InitialPass(file);
	}

	IncludesDb::PurgeIncludes(file, dbScp);
	InheritanceDb::PurgeInheritance(file, dbScp);
}

void MultiParse::RemoveAllDefs(const std::set<UINT>& fileIds, DTypeDbScope dbScp)
{
	try
	{
		if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
		{
			if (g_pGlobDic)
				g_pGlobDic->RemoveAllDefsFrom(fileIds);
		}

		if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem)
			SDictionary()->RemoveAllDefsFrom(fileIds);
	}
	catch (const UnloadingException&)
	{
		throw;
	}
#if !defined(SEAN)
	catch (...)
	{
		// ODS even in release builds in case VALOGEXCEPTION causes an exception
		CString msg;
		CString__FormatA(msg, "VA ERROR exception caught MP::RAD %d\n", __LINE__);
		OutputDebugString(msg);
		_ASSERTE(!"MultiParse::RemoveAllDefs exception");
		VALOGEXCEPTION("MP::RAD:");
	}
#endif // !SEAN

	{
		LogElapsedTime tt("MP::RAD rm inv", 2000);
		g_DBFiles.RemoveInvalidatedFiles();
	}

	IncludesDb::PurgeIncludes(fileIds, dbScp);
	InheritanceDb::PurgeInheritance(fileIds, dbScp);
}

void MultiParse::RemoveMarkedDefs(DTypeDbScope dbScp)
{
	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSolution)
		g_pGlobDic->RemoveMarkedDefs();
	if ((DWORD)dbScp & (DWORD)DTypeDbScope::dbSystem)
		SDictionary()->RemoveMarkedDefs();
}

// turns "foo < CString > f" into "foo%CString% f"
void EncodeTemplates(WTString& encStr)
{
	if (-1 == encStr.Find('<'))
		return;

	static thread_local WTString outStr; // temp string
	outStr.PreAllocBuffer(encStr.GetLength()); // this will clear str
	int inTemplate = 0;
	int opPos = -2;

	for (int len = encStr.GetLength(), i = 0; i < len; i++)
	{
		char c = encStr[i];
		if (c == '<')
		{
			if (-2 == opPos)
			{
				// only search for operator once
				opPos = encStr.Find("operator");
			}

			// [case: 1539] don't treat operator << as a template
			if (-1 == opPos || opPos > i || (opPos + 11) < i)
			{
				++inTemplate;

				// [case: 91733]
				// eat space before <
				if (outStr.GetLength() && DecodeChar(outStr[outStr.GetLength() - 1]) == ' ')
					outStr.SetAt(outStr.GetLength() - 1, EncodeChar(c));
				else
					outStr += EncodeChar(c);

				// eat initial space inside <
				while ((i + 1 < len) && encStr[i + 1] == ' ')
					++i;

				continue;
			}
		}

		if (inTemplate)
		{
			bool eatSpace = false;
			char nextCh = (i + 1 < len) ? encStr[i + 1] : 0;
			if (c == ' ' && strchr(" >,[]", nextCh))
			{
				eatSpace = true;
			}
			else if (c == ',')
			{
				// single space after comma: ", "
				outStr += EncodeChar(c);
				outStr += EncodeChar(' ');
				eatSpace = true;
			}
			else if (c == ':' && nextCh == ':')
			{
				// We need to replace all "::"'s with "."'s in templates
				outStr += EncodeChar('.');
				i++;
				eatSpace = true;
			}
			else if (c == '.')
			{
				outStr += EncodeChar(c);
				eatSpace = true;
			}
			else if (strchr("*&^%", c))
			{
				// single space before
				const char prevCh = outStr.GetLength() ? DecodeChar(outStr[outStr.GetLength() - 1]) : '\0';
				if (!strchr(" <,*&^%", prevCh)) // unless one these chars precede
					outStr += EncodeChar(' ');

				outStr += EncodeChar(c);

				// eat spaces
				while ((i + 1 < len) && encStr[i + 1] == ' ')
					++i;

				// single space after
				nextCh = (i + 1 < len) ? encStr[i + 1] : 0;
				if (!strchr(">,*&^%", nextCh)) // unless one of these chars follow
					outStr += EncodeChar(' ');
			}
			else
				outStr += EncodeChar(c);

			if (eatSpace)
			{
				while ((i + 1 < len) && encStr[i + 1] == ' ')
					++i;
			}
		}
		else
			outStr += c;

		if (c == '>' && inTemplate)
			inTemplate--;
	}

	encStr.AssignCopy(outStr);
}

void DecodeTemplatesInPlace(WTString &str, int devLang /*= gTypingDevLang*/)
{
	std::vector<int> encodedMemberOpPositions;

	int inTemplate = 0;
	str.CopyBeforeWrite();
	for (int l = str.GetLength(), i = 0; i < l; i++)
	{
		int en_offset = str[i] - g_encodeChar;
		if (en_offset >= 0 && en_offset < kEncodeCharsLen)
		{
			if (g_encodeChars[en_offset] == '<')
				++inTemplate;
			else if (g_encodeChars[en_offset] == '>')
				--inTemplate;

			str.SetAt_nocopy(i, g_encodeChars[en_offset]);

			if (inTemplate && g_encodeChars[en_offset] == '.')
				encodedMemberOpPositions.push_back(i);
		}
	}

	if (IsCFile(devLang) || -1 == devLang)
	{
		for (std::vector<int>::reverse_iterator it = encodedMemberOpPositions.rbegin();
		     it != encodedMemberOpPositions.rend(); ++it)
			str.ReplaceAt_new(uint(*it), 1, "::");
	}
}
WTString DecodeTemplates(const WTString& str, int devLang /*= gTypingDevLang*/)
{
	WTString ret = str;
	DecodeTemplatesInPlace(ret, devLang);
	return ret;
}

WTString StripEncodedTemplates(LPCSTR symscope)
{
	WTString sym;
	if (symscope)
	{
		int templateDepth = 0;
		static const char kOpenTemplateCh = EncodeChar('<');
		static const char kCloseTemplateCh = EncodeChar('>');
		for (int i = 0; symscope[i]; i++)
		{
			if (symscope[i] == ':')
				templateDepth = 0; // the way Jerry broke out of templates??
			else if (symscope[i] == kOpenTemplateCh)
			{
				++templateDepth;
				continue;
			}
			else if (symscope[i] == kCloseTemplateCh)
			{
				--templateDepth;
				continue;
			}

			if (!templateDepth)
				sym += symscope[i];
		}
	}
	return sym;
}

BOOL IsReservedWord(std::string_view sym, int devLang, bool typesShouldBeConsideredReserved /*= true*/)
{
	if (sym.starts_with(':'))
		sym = sym.substr(1);
	DType* data = GetDFileMP(devLang)->LDictionary()->FindExact(sym, 0);
	// pick out reserved words
	if (data)
	{
		switch (data->MaskedType())
		{
		case TYPE:
			if (!typesShouldBeConsideredReserved)
				return FALSE;
			return TRUE;

		case RESWORD:
			if (!typesShouldBeConsideredReserved && data->IsReservedType())
				return FALSE;
			return TRUE;
		}
	}

	return FALSE;
}

// GetTypesFromDef
// ----------------------------------------------------------------------------
// Reads the definition of a symbol and returns it's declare type:
// 	"extern CString* pStr =..." -> "CString"
//  "class foo : public bar, baz{...}" -> "bar[\f]baz"
// Returns a /f separated list.
// Excludes non-type, reserved words (const, *, &).
//
WTString GetTypesFromDef(LPCSTR symScope, LPCSTR symDef, int maskedType, int devLang)
{
	_ASSERTE((maskedType & TYPEMASK) == maskedType);
	// parse definition: either "type var=..." or "class name : base classes"
	WTString typeLst;
	// handle #define's
	token2 tdef(symDef);
	switch (maskedType)
	{
	case DEFINE: {
		LPCSTR p = strstr(symDef, "define");
		WTString defStr;
		WTString def;
		while (p)
		{
			p += 6;                             // past define
			while (wt_isspace4(*p)) // whitespace
				p++;
			while (ISCSYM(*p)) // get def
				p++;
			if (*p == '(')
			{
				// skip macro args: #define foo(x)
				p++;
				for (int parens = 1; *p && parens; ++p)
				{
					if (*p == '(')
						++parens;
					else if (*p == ')')
						--parens;
				}
			}
			while (wt_isspace4(*p)) // whitespace
				p++;

			LPCSTR bPos = p;
			while (*p && *p != '\f') // definition
				p++;
			def = std::string_view(bPos, p);

			// Look for C++ casts:
			// static_cast/dynamic_cast/const_cast/reinterpret_cast<type>
			LPCSTR castPos = strstr(def.c_str(), "_cast<");
			if (castPos)
				def.MidInPlace(TokenGetField2(&castPos[6], "< \t&*[]>"));

			if (def.GetLength())
			{
				if (!IsReservedWord(def, devLang, false))
				{
					defStr += def;
					defStr += '\f';
				}
			}
			//			else
			//				typeLst += WTString(WILD_CARD_SCOPE) + '\f'; // #define NONAMESPACE\n NONAMESPACE::foo// let
			// bcl look in global namespace

			p = strstr(p, "define");
		}
		tdef = std::move(defStr);
	}
	break;

	case NAMESPACE: {
		// namespace foo=boost::somenamespace
		LPCSTR p = strchr(symDef, '=');
		if (p)
			tdef = &p[1];
	}
	break;

	case FUNC:
	case VAR:
		// [case: 18035] don't do this on template defs
		if (!strchr(symDef, EncodeChar('>')))
		{
			// [case: 16790] remove params from function definition(s)
			// does not currently work when () are encoded
			const WTString theSym(StrGetSym(symScope));
			WTString def(symDef), newDef;
			uint32_t startPos = 0;
			int pos = def.Find(theSym, (int)startPos);
			while (-1 != pos)
			{
				const int pos1 = def.Find('(', pos + 1);
				if (-1 == pos1)
				{
					newDef += def.mid_sv(startPos);
					break;
				}

				const int pos2 = def.Find(')', pos1);
				if (-1 == pos2)
				{
					newDef += def.mid_sv(startPos);
					break;
				}

				// include () but skip everything between ( and )
				newDef += def.mid_sv(startPos, pos1 - startPos + 1);
				startPos = (uint32_t)pos2;
				pos = def.Find(theSym, (int)startPos);
				if (-1 == pos)
					newDef += def.mid_sv(startPos);
			}

			if (!newDef.IsEmpty())
				tdef = std::move(newDef);
		}
		break;

	case STRUCT: {
		WTString def(symDef);
		WTString tmp("declspec(");
		int pos = def.Find(tmp);
		if (-1 != pos)
		{
			// [case: 64203] struct __declspec(...) Foo : Bar{...}
			const WTString theSym(StrGetSym(symScope));
			WTString newDef;
			WTString curDef;
			while (tdef.more())
			{
				tdef.read('\f', curDef);
				LPCSTR s = strstrWholeWord(curDef, theSym);
				if (s)
				{
					int symPos = ptr_sub__int(s, curDef.c_str());
					if (pos < symPos)
						curDef.MidInPlace(symPos);
				}

				if (!curDef.IsEmpty())
				{
					newDef += curDef;
					newDef += '\f';
				}
			}

			tdef = std::move(newDef);
		}
	}
	break;

	case C_INTERFACE: {
		WTString def(symDef);
		// [case: 97120]
		static const WTString ifaceOpen("_INTERFACE(");
		int pos = def.Find(ifaceOpen);
		if (-1 != pos)
		{
			WTString newDef;
			WTString curDef;
			while (tdef.more())
			{
				tdef.read('\f', curDef);
				pos = curDef.Find(ifaceOpen);
				if (-1 != pos)
				{
					auto tmp = curDef.mid_sv(pos + ifaceOpen.GetULength());
					pos = (int)tmp.find('"');
					if (-1 != pos)
					{
						pos = (int)tmp.find('"', pos + 1u);
						if (-1 != pos)
						{
							pos = (int)tmp.find(')', (uint32_t)pos);
							if (-1 != pos)
							{
								curDef.MidInPlace(tmp.substr(pos + 1u));
								curDef.TrimLeft();
							}
						}
					}
				}
				else
				{
					pos = curDef.Find("interface");
					if (0 == pos || 8 == pos)
						curDef.Empty(); // "interface" || "typedef interface"
				}

				if (!curDef.IsEmpty())
				{
					newDef += curDef;
					newDef += '\f';
				}
			}

			tdef = std::move(newDef);
		}
	}
	break;
	}

	// for each def
	WTString def;
	WTString sym, currentDefTypes;
	while (tdef.more())
	{
		tdef.read('\f', def);
		DecodeScopeInPlace(def); // decode all but templates
		if (def.begins_with('.'))
			def.MidInPlace(1); // Fixes "vector<::myclass> foo;", "::" broke things

		EncodeTemplates(def);

		sym.Clear();
		currentDefTypes.Clear();

		// for each letter in current def
		for (uint i = 0; def[i]; /*i++*/)
		{
			uint listi = i;
			while (wt_isspace(def[i]))
				i++;
			while (def[i] && !strchr(" \t\r\n;:{}[]()+!@#%^&*\"'=<>,", def[i]))
			{
				bool hadNum = false;
				if (wt_isdigit(def[i]))
					hadNum = true;

				sym += def[i++];

				// [case:21320] moved '-' from the loop condition to special case it here.
				// we want to break if '-' is encountered in user code, but we use '-' in
				// scope; do not break from loop if '-' is ours (scope related).
				// assume is scope related if there is a digit immediately before
				// or after '-'
				if (def[i] == '-')
				{
					if (hadNum)
						sym += def[i++];
					else if (!wt_isdigit(def[i + 1]))
						break;
				}
			}

			while (wt_isspace(def[i]))
				i++;

			if (def[i] == '&' && def[i + 1] == '&')
			{
				// [case: 112952]
				// eat variadic args (_Types&&... Args)
				i += 2;
				while (wt_isspace(def[i]))
					i++;

				if (def[i] == '.' && def[i + 1] == '.' && def[i + 2] == '.')
				{
					i += 3;
					while (wt_isspace(def[i]))
						i++;

					while (def[i] && !strchr(" \t\r\n;:{}[]()+!@#%^&*\"'=<>,", def[i]))
						++i;

					sym.Clear();
					continue;
				}
			}

			// Oops, this brakes "DECLARE_INTERFACE() foo : base{}"
			// 			if(def[i] == '(')
			// 				break; // don't add arg types to type list
			// read symbols, convert "foo.bar" or "foo :: bar" to foo:bar
			if ((def[i] == ':' && def[i + 1] == ':') || def[i] == '.')
			{
				i++;
				if (def[i] == ':')
					i++;
				if (def[i] == '.')
					break; // {...}

				// 1) Fixes "typedef ::foo bar", so foo is not of type "typedef::bar"
				// 2) class foo : public ::Bar , foo is not derived from public::bar
				// 3) const ::GlobalCls foo;
				switch (sym[0])
				{
				case 'p':
					if (sym == "public" || sym == "private" || sym == "protected") // 2
					{
						sym.Clear();
						continue;
					}
					break;
				case 'c':
					if (sym == "const" || sym == "constexpr" || sym == "consteval" || sym == "constinit") // 3
					{
						sym.Clear();
						continue;
					}
					break;
				case '_':
					if (sym == "__published" || sym == "_CONSTEXPR17" || sym == "_CONSTEXPR20" ||
					    sym == "_CONSTEXPR20_CONTAINER") // 2, 3, 3, 3
					{
						sym.Clear();
						continue;
					}
					break;
				case 't':
					if (sym == "typedef" || sym == "thread_local") // 1, 3
					{
						sym.Clear();
						continue;
					}
					break;
				case 'i':
					if (sym == "internal") // 2
					{
						sym.Clear();
						continue;
					}
					break;
				case 's':
					if (sym == "static") // 3
					{
						sym.Clear();
						continue;
					}
					break;
				case 'e':
					if (sym == "extern") // 3
					{
						sym.Clear();
						continue;
					}
					break;
				}
				sym += DB_SEP_CHR;
				continue;
			}

			const int symLen = sym.GetLength();
			// symLen of 750 generates a lot of exceptions in Unreal with TFunctionMessageHandler
			if (symLen > 750/*1200*/)
			{
				WTString err;
				err.WTFormat("GTFD exception : too much \'%s\'\n", def.c_str());
				VALOGERROR(err.c_str());
				Log(err.c_str());
				throw WtException(err);
			}
			else if (symLen)
			{
				if (ISCSYM(sym[0]) || sym[0] == ':')
				{
					if (sym != StrGetSym(symScope))
					{
						if (IsReservedWord(sym, devLang, false))
						{
							if (sym == "throw" || sym == "restrict")
								break; // bug 2029 - don't add throw spec to type

							if (IsCFile(devLang))
							{
								// [case: 109637]
								// toss out whatever cruft was in def preceding keyword
								if (CLASS == maskedType && sym == "class")
									currentDefTypes.Clear();
								else if (STRUCT == maskedType && (sym == "struct" || sym == "union"))
									currentDefTypes.Clear();
								else if (NAMESPACE == maskedType && sym == "namespace")
									currentDefTypes.Clear();
								else if (C_INTERFACE == maskedType && sym == "interface")
									currentDefTypes.Clear();
							}
						}
						else
						{
							if (IsCFile(devLang) && sym.find("enable_if") == 0)
							{
								// [case: 142353]
								const WTString sym2 = DecodeTemplates(sym, devLang);

								// pull out second arg to enable_if
								const int commaPos = sym2.find(',');
								if (-1 != commaPos)
								{
									const int closeArg1Pos = sym2.Find('>', commaPos);
									if (-1 != closeArg1Pos)
									{
										const int closeArg2Pos = sym2.Find('>', closeArg1Pos);
										if (-1 != closeArg2Pos)
										{
											// create new sym using arg 2 and remainder of sym
											sym = sym2.mid_sv(commaPos + 1u, closeArg2Pos - 1u - commaPos + 1);
											sym.Trim();
											EncodeTemplates(sym);
										}
									}
								}
							}

							if (sym[0] != ':')
								sym.prepend(DB_SEP_CHR);
							sym += '\f';
							if (!typeLst.begins_with(sym) && (typeLst.Find2('\f', sym.c_str()) == -1))
							{
								if (currentDefTypes.IsEmpty() || (!currentDefTypes.begins_with(sym) && (currentDefTypes.Find2('\f', sym.c_str()) == -1)))
								{
									if ((devLang == JS || Is_Tag_Based(devLang)) && (sym == "var" || sym == "function"))
									{
										// Filter these in JS and HTML
									}
									else
									{
										// [case: 112952]
										// strip _NODISCARD
										// rather than a hardcoded compare, consider searching for sym;
										// if match is macro with no body, skip it
										if (sym[1] != '_' ||
										    (sym != ":_NODISCARD\f" && sym != ":_CONSTEXPR17\f" &&
										     sym != ":_CONSTEXPR20\f" && sym != ":_CONSTEXPR20_CONTAINER\f"))
											currentDefTypes += sym;
									}
								}
							}
						}
					}
				}

				sym.Clear();
			}

			if (def[i] == ',' && !IS_OBJECT_TYPE((uint)maskedType))
				break; // string foo, bar;

			if (!def[i] || def[i] == '=' || def[i] == '"' || def[i] == '\'')
			{
				if (def[i] != '=' || !Is_HTML_JS_VBS_File(devLang))
					break;
				// Continue on to use assigned value as type "foo = new type;"
			}

			if (!ISCSYM(def[i]) || listi == i)
				i++;
		}

		// finished with current def, save to typeLst
		typeLst += currentDefTypes;
		currentDefTypes.Clear();
	}

	typeLst.ReplaceAll('.', ':');
	return typeLst;
}

WTString GetTypesFromDef(const DType* dt, int maskedType, int devLang)
{
	try
	{
		dt->LoadStrs();
		return GetTypesFromDef(dt->SymScope(), dt->Def(), maskedType, devLang);
	}
	catch (const WtException&)
	{
	}
	return WTString();
}

// GetTypeFromDef
// ----------------------------------------------------------------------------
// Passed "const CRect *foo()" -> "const CRect *"
// Returns a single string that is the type - suitable for text building (refactoring).
// Includes reserved words (const, *, &) but not visibility/access.
//
WTString GetTypeFromDef(LPCSTR def, int devLang)
{
	WTString code = TokenGetField(def, "{}");
	if (-1 != code.Find('\f'))
	{
		// [case: 73114]
		// don't feed multiple definitions concat'd with \f to ReadToCls
		TokenGetField2InPlace(code, '\f');
	}

	if (-1 != code.Find('='))
	{
		// [case: 86091] remove default param values
		code = HandleDefaultValues(devLang, code, code.GetLength(), ";", FALSE);

		const int eqPos = code.Find('=');
		if (-1 != eqPos)
		{
			const int parenPos = code.Find('(');
			if (-1 == parenPos || eqPos < parenPos)
			{
				// [case: 76987] ignore everything right of = (assignment)
				// TODO: what this really needs is a SplitAssignment(const WTString& inCode, WTString &outLeft,
				// WTString& outRight);
				TokenGetField2InPlace(code, '=');
			}
		}
	}

	def = code.c_str();
	ReadToCls rtc(devLang);
	rtc.ReadTo(code, ";");
	WTString typeStr(def, ptr_sub__int(rtc.State().m_lastScopePos, def));
	if (typeStr.GetLength() == 0)
		typeStr = GetCStr(def); // Passed "Boolean", the above logic strips off the whole def

	if (typeStr == "class ")
	{
		// [case: 72883]
		// passed in "class vector : ..." typeStr is set to "class "
		// not a fix for ReadToCls but a workaround here
		if (0 == code.Find("class "))
		{
			code = code.Mid(5);
			code.TrimLeft();

			def = code.c_str();
			rtc.ReadTo(code, ";");
			WTString typeStr2(def, ptr_sub__int(rtc.State().m_lastScopePos, def));
			if (typeStr2.IsEmpty())
				typeStr = GetCStr(def);
			else
				typeStr += typeStr2;
		}
	}
	else if (typeStr == "operator ")
	{
		// [case: 27615] conversion operator:
		// operator LPWSTR() const throw() { ... }
		int pos = code.Find("()");
		if (code.Find("operator ") == 0 && -1 != pos)
		{
			typeStr = code.Mid(9, pos - 9);
			typeStr.TrimRight();
		}
	}
	else if (rtc.State().m_lastChar == ')' && typeStr.Find("operator(") == (typeStr.GetLength() - 9) &&
	         typeStr.Find("operator(") != -1)
	{
		// [case: 27615] operator()
		typeStr = typeStr.Left(typeStr.GetLength() - 9);
		typeStr.TrimRight();
	}

#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
	if (!typeStr.IsEmpty() && typeStr[0] == '_' && StartsWith(typeStr, "__property"))
	{
		// [case: 133417]
		typeStr = typeStr.Mid(10, typeStr.GetLength() - 10);
		typeStr.Trim();
		CleanupTypeString(typeStr, devLang);
		return typeStr;
	}
#endif

	if (rtc.State().m_lastChar == ']')
	{
		typeStr += '*';
		typeStr.ReplaceAll("* *", "**", FALSE);
	}

	CleanupTypeString(typeStr, devLang);
	return typeStr;
}

WTString CleanScopeForDisplay(LPCSTR scope)
{
	return CleanScopeForDisplayW(WTString(scope).Wide());
}

CStringW CleanScopeForDisplayW(LPCWSTR scope)
{
	int len = (int)wcslen(scope);
	CStringW buf;
	buf.Preallocate(len + 5);
	for (int i = 0; i < len; i++)
	{
		WCHAR c = scope[i];
		if (i == 0 && c == DB_SEP_CHR)
		{
			// eat leading ':'
		}
		else if (c == '-') // change foo.if-101.bar to foo.if.bar
		{
			const int oldI = i;
			while (wt_isdigit(scope[i + 1]))
				i++;
			if (oldI == i)
			{
				switch (scope[i + 1])
				{
				case '>':  // ->
				case '-':  // --
				case '=':  // -=
				case ':':  // -
				case '\0': // - (at end of str)
					buf += DecodeCharW(scope[i]);
					if (scope[i + 1] == '-')
						buf += DecodeCharW(scope[++i]); // --
					break;
				}
			}
		}
		else if (c == DB_SEP_CHR) // change foo:bar to foo.bar in C++
		{
			buf += '.';
		}
		// I kinda like the foo.bar better than foo::bar in minihelp
		//		else if(c == '.' && gTypingDevLang == Src)  // change foo.bar to foo::bar in C++
		//		{
		//			buf += "::";
		//		}
		else
			buf += DecodeCharW(scope[i]); // decode template chars
	}
	return buf;
}

// takes parser scope and turns it into a string that FindExact will work with.
// Removes :if-100:BRC-200:switch-300:
void ScopeToSymbolScope(WTString& scope)
{
	scope.ReplaceAll('.', ':');
	for (;;)
	{
		int pos = scope.ReverseFind('-');
		if (-1 != pos)
		{
			if (::wt_isdigit(scope[pos + 1]))
			{
				const int pos2 = scope.Find(DB_SEP_STR, pos);
				if (-1 != pos2)
				{
					const int pos3 = scope.Find(DB_SEP_STR, pos2 + 1);
					if (-1 == pos3)
					{
						const WTString tmp(scope.Mid(pos2));
						scope = scope.Left(pos);
						scope += tmp;
						continue;
					}
				}
			}
		}

		pos = scope.ReverseFind(":for:");
		if (-1 != pos && pos == scope.GetLength() - 5)
		{
			scope = scope.Left(pos + 1);
			continue;
		}

		pos = scope.ReverseFind(":BRC:");
		if (-1 != pos && pos == scope.GetLength() - 5)
		{
			scope = scope.Left(pos + 1);
			continue;
		}

		pos = scope.ReverseFind(":if:");
		if (-1 != pos && pos == scope.GetLength() - 4)
		{
			scope = scope.Left(pos + 1);
			continue;
		}

		pos = scope.ReverseFind(":while:");
		if (-1 != pos && pos == scope.GetLength() - 7)
		{
			scope = scope.Left(pos + 1);
			continue;
		}

		pos = scope.ReverseFind(":else:");
		if (-1 != pos && pos == scope.GetLength() - 6)
		{
			scope = scope.Left(pos + 1);
			continue;
		}

		pos = scope.ReverseFind(":switch:");
		if (-1 != pos && pos == scope.GetLength() - 8)
		{
			scope = scope.Left(pos + 1);
			continue;
		}

		break;
	}
}

WTString GetDefsFromMacro(const WTString& sScope, const WTString& sDef, int langType, MultiParse* mp)
{
	// #define MYPROC MyProc
	// MYPROC( // list args of MyProc
	WTString apiDef;

	// sDef might be \f deliminated
	token2 defs = sDef;
	WTString def;
	while (defs.more() > 1)
	{
		defs.read('\f', def);

		int defPos = def.Find("define");
		if (defPos < 0)
			continue;

		defPos += 6;
		WTString defVal = def.Mid(defPos);

		bool isOnlyCSymOrWhitespace = true;
		for (int i = 0; i < defVal.GetLength(); ++i)
		{
			char ch = defVal[i];
			if (!wt_isspace(ch) && !ISCSYM(ch))
			{
				isOnlyCSymOrWhitespace = false;
				break;
			}
		}
		if (!isOnlyCSymOrWhitespace)
			continue;

		token2 reader = defVal;
		WTString macroSymbol = reader.read(" \t\r\n");
		if (!macroSymbol.GetLength())
			continue;

		// Make sure def is not empty, and doesn't contain whitespace
		WTString macroDef = reader.read("\r\n"); // to EOL
		macroDef.Trim();
		if (!macroDef.GetLength())
			continue;
		if (macroDef.FindOneOf(" \t") >= 0)
			continue;

		DType* apidata = mp->FindSym(&macroDef, &mp->m_lastScope, &mp->m_baseClassList);
		if (apidata)
		{
			WTString def2(apidata->Def());
			def2.Trim();
			if (def2.GetLength())
				apiDef += def2 + '\f';
		}
	}

	return apiDef;
}

WTString CleanDefForDisplay(LPCSTR scope, int devLang)
{
	int len = strlen_i(scope);
	bool inString = false;
	bool inNumber = false;
	int inTemplate = 0;
	char lastCh = '\0';
	WTString buf;
	buf.PreAllocBuffer(len + 5);
	for (int i = 0; i < len; i++)
	{
		char c = scope[i];
		if (c != '\f' && wt_isspace(c)) // change foo.if-101.bar to foo.if.bar
		{
			buf += ' ';
			while (scope[i + 1] != '\f' && wt_isspace(scope[i + 1]))
				i++;
		}
		else if (c == '.' && (devLang == Src || devLang == Header) && !inString &&
		         !inNumber) // change foo.bar to foo::bar in C++
		{
			if (scope[i + 1] == '.') // {...}
			{
				while (scope[i + 1] == '.')
					i++;
				buf += "...";
			}
			else if (inTemplate)
				buf += "::";
			else
				buf += "."; // "::"; no longer need to change to '.' to '::' Case:2664
		}
		else if (c == 0x11 && inTemplate && (devLang == Src || devLang == Header) && !inString &&
		         !inNumber) // change foo.bar to foo::bar in C++
		{
			// 0x11 is encoded '.'
			if (scope[i + 1] == 0x11)
			{
				i++;
				buf += "::";
			}
			else
				buf += "::";
		}
		else
		{
			if ((c == '\"' || c == '\'') && lastCh != '\\')
				inString = !inString;

			if (c == '.' && inNumber)
				inNumber = false;
			else if (wt_isdigit(c) && !wt_isalpha(lastCh))
			{
				// if first lone digit, set inNumber = true but not for
				// subsequent ones so that foo12 doesn't set it incorrectly
				if (!wt_isdigit(lastCh))
					inNumber = true;
			}
			else
				inNumber = false;

			char nextCh = DecodeChar(c); // decode template chars
			buf += nextCh;

			if (!inString)
			{
				// this will be incorrect if comparison angle brackets are present in scope
				if (nextCh == '<')
					++inTemplate;
				if (nextCh == '>')
					--inTemplate;
			}
		}
		lastCh = c;
	}
	return buf;
}

//////////////////////////////////////////////////////////////////////////
void DB_Iterator::IterateDB(VAHashTable* db)
{
	try
	{
		for (DefObj* sym = db->FindRowHead(m_hashID); !m_break && sym; sym = (sym->next != sym) ? sym->next : NULL)
			OnSym(sym);
	}
	catch (...)
	{
		_ASSERTE(!"IterateDB");
	}
}
void DB_Iterator::Iterate(MultiParsePtr mp, const WTString& sym)
{
	DB_READ_LOCK;
	m_mp = mp;
	m_sym = sym;
	m_hashID = DType::HashSym(sym);
	m_break = FALSE;
	for (m_DB_ID = MultiParse::DB_VA; !m_break && m_DB_ID <= MultiParse::DB_SYSTEM; m_DB_ID++)
	{
		if (m_DB_ID == MultiParse::DB_VA) // .VA files
		{
			CLockMsgCs lck(*GetDFileMP(m_mp->FileType())->LDictionary()->GetDBLock());
			IterateDB(GetDFileMP(m_mp->FileType())->LDictionary()->GetHashTable());
		}
		else if (m_DB_ID == MultiParse::DB_LOCAL) // Local DB
		{
			CLockMsgCs lck(*m_mp->LDictionary()->GetDBLock());
			IterateDB(m_mp->LDictionary()->GetHashTable());
		}
		else if (m_DB_ID == MultiParse::DB_GLOBAL) // Global DB
			IterateDB(g_pGlobDic->GetHashTable());
		else if (m_DB_ID == MultiParse::DB_SYSTEM) // System DB
			IterateDB(m_mp->SDictionary()->GetHashTable());
		OnNextDB(m_DB_ID);
	}
}

void LoadMiscFileTypes(int dbID, int loadFType)
{
	FileList files;
	FindFiles(VaDirs::GetUserDir() + L"misc\\", L"*.*", files);
	FileList defaultFiles;
	FindFiles(VaDirs::GetDllDir() + L"misc\\", L"*.*", defaultFiles);

	if (files.size())
	{
		// user has some overrides - merge in non-conflicting defaults
		for (FileList::const_iterator defaultIt = defaultFiles.begin(); defaultIt != defaultFiles.end(); ++defaultIt)
		{
			CStringW basename(::GetBaseName((*defaultIt).mFilenameLower));
			for (FileList::const_iterator filesIt = files.begin(); filesIt != files.end(); ++filesIt)
			{
				CStringW curBasename(::GetBaseName((*filesIt).mFilenameLower));
				if (curBasename == basename)
				{
					basename.Empty();
					break;
				}
			}

			if (!basename.IsEmpty())
				files.Add(*defaultIt);
		}
	}
	else
	{
		// no user overrides at all
		files.swap(defaultFiles);
	}

	for (FileList::const_iterator it = files.begin(); it != files.end(); ++it)
	{
		if (GetFileType((*it).mFilename) == loadFType)
		{
			const CStringW file((*it).mFilenameLower);
			if (file.Find(L"stdafx") == -1) // assume already parsed any file with stdafx in the name
				GetDFileMP(dbID)->FormatFile((*it).mFilename, V_SYSLIB | V_VA_STDAFX, ParseType_Locals);
		}
	}
}

void LoadInstallDirMiscFiles(int ftype)
{
	if (sMiscFilesLoaded[ftype])
		return;

	sMiscFilesLoaded[ftype] = true;

	if (ftype == RC || ftype == Idl)
		LoadMiscFileTypes(ftype, Header);
	else
		LoadMiscFileTypes(ftype, ftype);

	if (ftype == HTML || ftype == PHP || ftype == ASP)
	{
		// Load *.js into .html and .php DFileDB's
		LoadMiscFileTypes(JS, JS);    // Load JS for <JS script> blocks
		LoadMiscFileTypes(VBS, VBS);  // Load VB for <VB script> blocks
		LoadMiscFileTypes(VBS, JS);   // Fill VBS with JS symbols
		LoadMiscFileTypes(ftype, JS); // Fill current lang with JS symbols
	}
}

void CleanupTypeString(WTString& typeStr, int fType)
{
	if (typeStr.ReplaceAll("virtual", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("abstract", "", TRUE))
		typeStr.Trim();
	if (typeStr.ReplaceAll("final", "", TRUE))
		typeStr.TrimRight();
	if (typeStr.ReplaceAll("sealed", "", TRUE))
		typeStr.TrimRight();
	if (typeStr.ReplaceAll("override", "", TRUE))
		typeStr.TrimRight();
	if (typeStr.ReplaceAll("inline", "", TRUE)) // [case: 51223]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_inline", "", TRUE)) // [case: 87897]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("__inline", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("__forceinline", "", TRUE)) // [case: 87897]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("AFX_INLINE", "", TRUE)) // [case: 87897]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_AFX_INLINE", "", TRUE)) // [case: 87897]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_AFX_PUBLIC_INLINE", "", TRUE)) // [case: 87897]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("ATLTYPES_INLINE", "", TRUE)) // [case: 33485]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CRT_STDIO_INLINE", "", TRUE)) // vs2015
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("private", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("public", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("__published", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("protected", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("tile_static", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("static", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("thread_local", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("internal", "", TRUE)) // [case: 34623]
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("mutable", "", TRUE)) // [case: 32586] handle mutable like static
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("extern", "", TRUE)) // [case: 65497] handle extern like static
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CRTIMP", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CRT_JIT_INTRINSIC", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CRTNOALIAS", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CRTRESTRICT", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("__cdecl", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("__CRTDECL", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("WINAPI", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("WINGDIAPI", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("WINUSERAPI", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("partial", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_Check_return_", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_Check_return_impl_", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_Check_return_opt_", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_Check_return_wat_", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("__checkReturn", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("__checkReturn_opt", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_NODISCARD", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CONSTEXPR17", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CONSTEXPR20_CONTAINER", "", TRUE))
		typeStr.TrimLeft();
	if (typeStr.ReplaceAll("_CONSTEXPR20", "", TRUE))
		typeStr.TrimLeft();

	typeStr.ReplaceAll("async", "", TRUE);

	typeStr.Trim();
	if (typeStr == "STDMETHOD(")
		typeStr = "HRESULT";

	// should this be a call to DecodeTemplates?  What if return type is a template?
	typeStr = DecodeScope(typeStr);

	// strip foreach syntax "string str in array" needs to return "string"
	if (Is_C_CS_File(fType))
	{
		int forEachPos = typeStr.ReverseFind(" in ");
		if (forEachPos != -1)
		{
			if (forEachPos != (typeStr.GetLength() - 4))
				forEachPos = -1;
		}

		if (forEachPos == -1)
		{
			forEachPos = typeStr.ReverseFind(" in");
			if (forEachPos != -1 && forEachPos == (typeStr.GetLength() - 3))
			{
				typeStr = typeStr.Left(forEachPos);
				forEachPos = typeStr.ReverseFind("^");
				if (forEachPos != -1)
					typeStr = typeStr.Left(forEachPos + 1); // [case: 14864] retain managed ref ^
				else
				{
					forEachPos = typeStr.ReverseFind(" ");
					if (forEachPos != -1)
						typeStr = typeStr.Left(forEachPos);
				}
			}
		}

		if (CS == fType && !typeStr.IsEmpty() && typeStr[0] == 'o')
		{
			int outPos = typeStr.Find("out ");
			if (0 == outPos)
			{
				// [case: 116073]
				typeStr = typeStr.Mid(4);
			}
		}

		if (IsCFile(fType))
		{
			// [case: 18573] fix up ":foo" --> "::foo"
			typeStr.ReplaceAll(":", "::");
			// correct affect of previous line on pre-existing "::"
			typeStr.ReplaceAll("::::", "::");
			// fix up "std::vector<std.string>" --> "std::vector<std::string>"
			typeStr.ReplaceAll("..", "::");
			typeStr.ReplaceAll(".", "::");
		}
	}
}

int GetTypeOfUnrealAttribute(LPCSTR txt)
{
	if (!txt)
		return UNDEF;
	if (txt[0] != 'U')
		return UNDEF;
	if (StartsWith(txt, "UINTERFACE", TRUE))
		return C_INTERFACE;
	if (StartsWith(txt, "UCLASS", TRUE))
		return CLASS;
	if (StartsWith(txt, "USTRUCT", TRUE))
		return STRUCT;
	if (StartsWith(txt, "UFUNCTION", TRUE))
		return FUNC;
	if (StartsWith(txt, "UDELEGATE"))
		return DELEGATE;
	if (StartsWith(txt, "UENUM", TRUE))
		return C_ENUM;
	if (StartsWith(txt, "UPROPERTY", TRUE))
		return PROPERTY;
	return UNDEF;
}

int GetTypeOfUnrealAttribute(LPCWSTR txt)
{
	if (!txt)
		return UNDEF;
	if (txt[0] != L'U')
		return UNDEF;
	if (StartsWith(txt, L"UINTERFACE", TRUE))
		return C_INTERFACE;
	if (StartsWith(txt, L"UCLASS", TRUE))
		return CLASS;
	if (StartsWith(txt, L"USTRUCT", TRUE))
		return STRUCT;
	if (StartsWith(txt, L"UFUNCTION", TRUE))
		return FUNC;
	if (StartsWith(txt, L"UDELEGATE"))
		return DELEGATE;
	if (StartsWith(txt, L"UENUM", TRUE))
		return C_ENUM;
	if (StartsWith(txt, L"UPROPERTY", TRUE))
		return PROPERTY;
	return UNDEF;
}

#include "CommentSkipper.h"
WTString RemoveComments(const WTString& str, int devLang /*= Src*/)
{
	WTString ret;
	char* ret_begin = ret.GetBuffer(str.GetLength());
	char* ret_it = ret_begin;
	const char* str_end = str.c_str() + str.length();

	CommentSkipper cs(devLang);
	char last_ch = 0;
	bool was_in_comment = false;
	for (const char* str_it = str.c_str(); str_it != str_end; ++str_it)
	{
		if (!cs.IsCode2(str_it[0], str_it[1]))
		{
			was_in_comment = true;
			continue;
		}

		if (was_in_comment && isalnum(last_ch) && isalnum(str_it[0]))
			*ret_it++ = ' '; // insert space if removing comment would result in concatination of words
		was_in_comment = false;
		*ret_it++ = last_ch = str_it[0];
	}

	ret.ReleaseBuffer(int(ret_it - ret_begin));
	return ret;
}

WTString RemoveCommentsAndSpaces(const WTString& str, int devLang /*= Src*/)
{
	WTString ret = RemoveComments(str, devLang);
	char* ret_begin = ret.GetBuffer(ret.GetLength());
	char* ret_end = ret_begin + ret.GetLength();

	std::replace_if(
	    ret_begin, ret_end, [](char c) { return c == '\t' || c == '\r' || c == '\n'; }, ' ');
	ret_end = std::unique(ret_begin, ret_end, [](char c1, char c2) { return c1 == ' ' && c2 == ' '; });

	ret.ReleaseBuffer(int(ret_end - ret_begin));
	ret.TrimLeft();
	ret.TrimRight();
	return ret;
}
