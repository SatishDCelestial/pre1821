#pragma once

// AutotextManager.h : header file
//

#include <afxtempl.h>
#include <map>
#include "WTString.h"
#include "FileTypes.h"
#include "vsFormatOption.h"
#include "EdCnt_fwd.h"
#include "../vate/AutoTextDlgParams.h"
#include "Library.h"

class ExpansionData;
class token;

namespace OWL
{
class TRegexp;
}

struct TemplateItem
{
	TemplateItem()
	{
	}
	TemplateItem(const WTString title, const WTString shortCut, const WTString src)
	    : mTitle(title), mShortcut(shortCut), mSource(src)
	{
	}
	TemplateItem(const WTString title, const char* shortCut, const char* src)
	    : mTitle(title), mShortcut(shortCut), mSource(src)
	{
	}
	TemplateItem(const char* title, const char* src, const char* desc = NULL, const char* shortCut = NULL)
	    : mTitle(title), mShortcut(shortCut), mSource(src), mDescription(desc)
	{
	}

	WTString mTitle;
	WTString mShortcut;
	WTString mSource;
	WTString mDescription;
	char mSpecialShortcut = '\0';
};

class AutotextManager
{
	friend class CAutotextPartKeyword;

  public:
	static void CreateAutotextManager();
	static void DestroyAutotextManager();

	static void InstallDefaultItems();
	static bool EditUserInputFieldsInEditor(EdCntPtr ed, const WTString& itemSource);

	static ReservedString gReservedStrings[];

  public:
	BOOL Insert(EdCntPtr ed, int idx) const;
	BOOL InsertText(EdCntPtr ed, const WTString& txt, bool isAutotextPoptype) const;
	BOOL InsertAsTemplate(EdCntPtr ed, const WTString& templateText, BOOL reformat = FALSE,
	                      const WTString& templateTitle = NULLSTR) const;

	void Load(int type);
	bool IsLoaded() const;
	void Unload();
	static void Edit(int type, LPCSTR snipName, const WTString& snipText);
	static void Edit(int type = 0, LPCSTR snipName = NULL, LPCSTR snipText = NULL)
	{
		Edit(type, snipName, WTString(snipText));
	}
	static void EditRefactorSnippets(int type = 0);
	static bool IsReservedString(const WTString& match);
	static void ExpandReservedString(EdCntPtr ed, WTString& in_out, const GUID* guid = nullptr,
	                                 const SYSTEMTIME* time = nullptr);
	static bool GetGuid(GUID& outGuid, const GUID* guidOverride = nullptr);
	static bool GetDateAndTime(SYSTEMTIME& outDT, const SYSTEMTIME* dtOverride = nullptr);
	static void ForEachReservedString(
	    std::function<bool(const ReservedString*, // pointer to reserved string
	                       char, // current modifier: 'D', 'U', 'L', 'C', 'P' (Default, Upper, Lover, Camel, Pascal)
	                       const WTString& // full keyword (name + modifier)
	                       )>
	        func);

	static void ApplyVAFormatting(CStringW& sourceW, CStringW& selectionW, CStringW& clipboardW, EdCnt* ed);
	static bool IsUserInputSnippet(const WTString& input);

	int GetCount() const
	{
		return (int)mTemplateCollection.GetSize();
	}
	WTString GetTitle(int idx, bool skipRequiredDefaultItems) const;
	WTString GetSource(int idx) const;
	WTString GetSource(LPCTSTR title);
	WTString GetSource(const WTString& title)
	{
		return GetSource(title.c_str());
	}
	bool HasShortcut(int idx) const
	{
		return !GetShortcut(idx).IsEmpty();
	}
	WTString GetShortcut(int idx) const;
	bool DoesItemUseString(int idx, LPCTSTR str) const;

	WTString GetTitleOfFirstSourceMatch(const WTString& search) const;

	// pass in pos set to -1 the first time
	WTString FindNextShortcutMatch(const WTString& potentialShortcut, int& pos);
	int GetSnippetsForShortcut(const WTString& shortcut, std::list<int>& matches);
	int GetSurroundWithSnippetsForSpecialShortcut(char shortcutKeyToMatch, std::list<int>& matches);

	bool IsItemTitle(const WTString& txt) const;
	int GetItemIndex(const char* title) const;
	int GetItemIndex(const WTString& title) const
	{
		return GetItemIndex(title.c_str());
	}
	void AddAutoTextToExpansionData(const WTString& potentialShortcut, ExpansionData* m_lst, BOOL addAll = FALSE);

	LPCWSTR QueryStatusText(DWORD cmdId, DWORD* statusOut);
	HRESULT Exec(EdCntPtr ed, DWORD cmdId);

  private:
	AutotextManager();
	virtual ~AutotextManager();

	int mLangType;
	mutable WTString mDeletedEdSelection;

	BOOL DoInsert(int templateIdx, EdCnt* ed, const WTString& kItemSource, vsFormatOption reformat,
	              const WTString& title = NULLSTR) const;
	int ProcessTemplate(int templateIdx, WTString& outInsertText, EdCnt* ed,
	                    const WTString& snippetTitle = NULLSTR) const;
	int MakeUserSubstitutions(token& partiallySubstitutedTemplate, const WTString& templateTitle, EdCnt* ed) const;

	void AppendDefaultItem(const TemplateItem* item);
	void CheckForDefaults(int langType);
	void EnsureTitleIsUnique(WTString& newTitle);
	void PopulateSnippetFile(CStringW templateFile);
	void LoadVate();

	FILETIME mLocalTemplateFileTime;
	CStringW mLocalTemplateFile;
	FILETIME mSharedTemplateFileTime;
	CStringW mSharedTemplateFile;
	Library mVateDll;

	CArray<TemplateItem, TemplateItem&> mTemplateCollection;

	struct DynamicSelectionCmdInfo
	{
		int mAutotextIdx;
		CStringW mMenutext;
	};
	typedef std::map<int, DynamicSelectionCmdInfo> DynamicSelectionCmds;
	DynamicSelectionCmds mDynamicSelectionCmdInfo;
};

class AutotextFmt;
typedef std::shared_ptr<AutotextFmt> ATextFmtPtr;

class AutotextFmt
{
  public:
	enum Casing : unsigned char
	{
		None = 0,

		Default = 0x01,

		UPPERCASE = 0x02,
		lowercase = 0x04,
		camelCase = 0x08,
		PascalCase = 0x10,

		All = 0xFF
	};

	struct Modifier
	{
		typedef std::function<void(WTString&)> FmtFnc;
		typedef std::shared_ptr<Modifier> Ptr;

		bool IsKeywordType;
		WTString Keyword;
		WTString Name;
		FmtFnc Fnc;
		WTString Rgx;

		Modifier();
		Modifier(const WTString& rgx, FmtFnc fnc);
		Modifier(const WTString& kw, const WTString& name, FmtFnc fnc, bool name_casing_by_kw = true);

		operator bool() const;

		void operator()(WTString& str) const;

		bool IsKeyword();
		void SetRegex(LPCSTR newRegex);
		void SetKeyword(const WTString& newKW, bool inherit_casing = true);
	};

	typedef std::function<void(WTString&)> InputFnc;

	typedef std::function<LPCSTR(LPCSTR)> MatchFnc;
	typedef std::function<LPCWSTR(LPCWSTR)> MatchFncW;

	WTString Keyword;
	mutable WTString Replacement;
	mutable WTString SearchRegex;

	MatchFnc MatchSolver;   // if match solver is assigned, all other settings are ignored!!!
	MatchFncW MatchSolverW; // if match solver is assigned, all other settings are ignored!!!

	InputFnc GetReplacement; // use this lambda if getting replacement is expensive, so that it is taken only if really
	                         // needed
	Modifier DefaultModifier;
	std::vector<Modifier::Ptr> Modifiers;

	AutotextFmt();
	AutotextFmt(const AutotextFmt&) = delete;
	AutotextFmt& operator=(const AutotextFmt&) = delete;

	virtual ~AutotextFmt();

	void SetBaseKeyword(LPCSTR keyword, bool apply_casings_to_modifiers = true);
	void AddModifierRgx(LPCSTR rgx, Modifier::FmtFnc fnc);
	void AddModifier(const WTString& modif, Modifier::FmtFnc fnc);

	// Method: AddModifiers
	// Description: Suppose that we have a keyword: SOLUTION_PATH_UPPER
	// modifier_casing: defines that keyword is UPPER CASE, so added modifiers will be _UPPER, _LOWER etc.
	// casing_flags: defines which modifiers we want to add, use All or any OR-ed combination
	// name: defines a base name of modifier, in our example it is PATH, (PATH_UPPER is full name of modifier)
	// fnc: defines lambda that modifies copy of Replacement into correct form, in our example it extracts PATH from
	// passed filename NOTE: modifier_casing and name are independent settings, if you set modifier casing to lowercase
	// and pass "Path" as name,
	//       your resulting working keyword will be $SOLUTION_Path_upper$ which is unlikely to exist!!!
	//       Be sure to pass correct settings, for speed purposes there are no casing checks.
	void AddModifiers(Casing modifier_casing, Casing casing_flags, LPCSTR name = NULL,
	                  Modifier::FmtFnc fnc = Modifier::FmtFnc());
	void AddModifiers(Casing modifier_casing, DWORD casing_flags, LPCSTR name = NULL,
	                  Modifier::FmtFnc fnc = Modifier::FmtFnc());
	void AddModifiersByKeyword(Casing casing_flags = All, LPCSTR name = NULL,
	                           Modifier::FmtFnc fnc = Modifier::FmtFnc());

	// 'D' => Default,
	// 'U' => UPPER_CASE
	// 'L' => lower_case
	// 'C' => camelCase
	// 'P' => PascalCase
	void AddModifiers(CHAR modifier_casing_char, LPCSTR casing_str, LPCSTR name = NULL,
	                  Modifier::FmtFnc fnc = Modifier::FmtFnc());
	void AddModifiersByKeyword(LPCSTR casing_str, LPCSTR name = NULL, Modifier::FmtFnc fnc = Modifier::FmtFnc());

	bool IsWithinCSTR(LPCSTR buffer, bool use_stl = true) const;

	bool IsWithin(const token& buffer) const;
	bool IsWithin(const WTString& buffer) const;
	bool IsWithinWide(const CStringW& buffer) const;

	void ReplaceAll(token& buffer) const;
	void ReplaceAll(WTString& buffer) const;
	void ReplaceAllWide(CStringW& buffer) const;

	static ATextFmtPtr FromKeyword(LPCSTR keyWord, LPCSTR replacement = nullptr);
	static ATextFmtPtr FromKeywordAndCasings(LPCSTR keyWord, LPCSTR replacement = nullptr, LPCSTR modifiers = "DULCP");
	static ATextFmtPtr FromKeywordAndCasingsWithSolver(LPCSTR keyWord, MatchFnc solver, LPCSTR modifiers = "DULCP");
	static ATextFmtPtr FromKeywordWithSolver(LPCSTR keyWord, MatchFnc solver);
	static ATextFmtPtr FromRegexWithSolver(LPCSTR re_pattern, MatchFnc match_solver);

	static Casing DetectCasing(LPCSTR text);
	static WTString ApplyCasing(Casing c, WTString&& str);
	static CStringW ApplyCasing(Casing c, CStringW&& str);

	static Casing ParseCasingChar(CHAR ch);
	static Casing ParseCasingStr(LPCSTR casingStr = "DULCP");

	static CStringW MakeCamel(CStringW&& str);
	static CStringW MakePascal(CStringW&& str);

	static CStringW MakeUpper(CStringW&& str);
	static CStringW MakeLower(CStringW&& str);

	static WTString MakeUpperWT(WTString&& str);
	static WTString MakeLowerWT(WTString&& str);

	static WTString MakeCamelWT(WTString&& str);
	static WTString MakePascalWT(WTString&& str);

  private:
	// returns Replacement if not empty, else invokes GetReplacement first
	// NOTE: this method is called during Replacing process
	const WTString& GetModifierInputString() const;

	// returns Modifier corresponding to current match
	// NOTE: this method is called during Replacing process
	const Modifier& GetModifier(LPCSTR match) const;
};

// [case: 37970]
// encode user strings that are used in generated autotext so that
// autotext processing doesn't inadvertently convert
WTString EncodeUserText(WTString txt);
WTString DecodeUserText(WTString txt);

WTString GetPotentialShortcutExt(const WTString& potentialShortcut);
WTString GetBodyAutotextItemTitle(BOOL isNetSym, bool inheritsFromUeClass = false);
WTString GetClassAutotextItemTitle();
WTString HideVaSnippetEscapes(const WTString& str);

extern AutotextManager* gAutotextMgr;
extern LPCTSTR kAutotextKeyword_Selection;
extern LPCTSTR kAutotextKeyword_Clipboard;
