#pragma once

#include "EdCnt_fwd.h"
#include "FileTypes.h"
#include "FileLineMarker.h"

enum
{
	SUGGEST_NOTHING,
	SUGGEST_TEXT = 1,
	SUGGEST_MEMBERS = 2,
	SUGGEST_AUTOTEXT = 4,
	SUGGEST_SYMBOLS = 0x8,
	SUGGEST_FILE_PATH = 0x10,
	SUGGEST_SNIPPET = 0x20,
	SUGGEST_TEXT_IF_NO_VS_SUGGESTIONS = 0x40,
	SUGGEST_ALL = 0xff
};

enum ScopeSuggestionMode
{
	smNone,
	smAssignment,
	smNew,
	smParam,
	smScope,
	smReturn,
	smType,
	smSwitchCase,
	smHashtag,
	smPropertyAttribute,

	smUe4attrFirst,
	smUe4attrClass,
	smUe4attrStruct,
	smUe4attrInterface,
	smUe4attrFunction,
	smUe4attrDelegate,
	smUe4attrEnum,
	smUe4attrProperty,
	smUe4attrParam,
	smUe4attrMeta,
	smUe4attrClassMeta,
	smUe4attrStructMeta,
	smUe4attrInterfaceMeta,
	smUe4attrFunctionMeta,
	smUe4attrDelegateMeta,
	smUe4attrEnumMeta,
	smUe4attrPropertyMeta,
	smUe4attrParamMeta,
	smUe4attrNoSuggestion,
	smUe4attrLast
};

// Simple class to override all MiniHelp/goto logic
class MiniHelpInfo
{
  private:
	INT m_MiniHelpType;
	WTString m_MiniHelpContext, m_MiniHelpDef;

  public:
	void SetMiniHelpInfo(LPCSTR context, LPCSTR def, int type);
	BOOL HasMiniHelpInfo()
	{
		return m_MiniHelpType != 0;
	}
	BOOL UpdateMiniHelp();
	BOOL GoToDef(); // Override goto for items entered with SetMiniHelpInfo();
};

class ScopeInfo;
using ScopeInfoPtr = std::shared_ptr<ScopeInfo>;

// ScopeInfo contains VAParse scope info
class ScopeInfo : public MiniHelpInfo
{
  public:
	ScopeInfo(int fType);
	ScopeInfo();
	~ScopeInfo() = default;

	void InitScopeInfo();
	bool HtmlSuggest(EdCntPtr ed);

	WTString m_Scope;
	WTString m_LastWord;
	WTString m_stringText;
	int m_suggestionType;
	ScopeSuggestionMode mScopeSuggestionMode;

	// EdCntrl Members
	WTString m_ParentScopeStr;

	// Mparse members
	DTypeList mCwDataList;

	BOOL m_showRed = true;
	BOOL m_isDef; // used to tell wether we should fix case
	BOOL m_xref;
	BOOL m_inParamList = FALSE;
	BOOL m_isMethodDefinition; // true if inclass == deep ie global scope or top of class
	BOOL m_inClassImplementation = FALSE;
	int m_line = 0;
	int m_scopeType; // used to test if in comment, string, or # preproc, ...
	int m_argParenOffset;
	int m_argCount = 0; // which arg the cursor is on
	int m_inParenCount; // number of parens in, used with paren overtype in edcnt2
	int m_firstVisibleLine;
	int m_lastErrorPos; // index of the last parse OnError();
	WTString m_lastScope;
	WTString m_baseClass;
	WTString m_baseClassList;
	WTString m_firstWord;
	WTString m_argTemplate; // "foo(int, WTString, ...);"
	WTString m_argScope;    // scope of foo "TFoo::foo"
	WTString m_xrefScope;
	// Snippet helpers
	WTString m_MethodName;
	WTString m_ClassName;
	WTString m_BaseClassName;
	WTString m_NamespaceName;
	WTString m_MethodArgs;
	WTString m_ScopeSuggestions;
	char m_commentType;
	char m_commentSubType;

	bool IsSysFile() const
	{
		return m_isSysFile;
	}
	UINT GetFileID() const
	{
		return m_fileId;
	}
	bool GetParseAll() const
	{
		return m_parseAll;
	}
	ParseType GetParseType() const
	{
		return m_parseType;
	}
	void GetGotoMarkers(LineMarkers& markers) const;
	BOOL IsWriteToDFile() const
	{
		return m_writeToDFile;
	}
	void SetFileType(int ftype)
	{
		m_ftype = ftype;
	}
	int FileType() const
	{
		_ASSERTE(m_ftype);
		return m_ftype;
	}
	CStringW GetFilename()
	{
		AutoLockCs l(mFilenameLock);
		return m_fileName;
	}
	void SetFilename(const CStringW& f)
	{
		AutoLockCs l(mFilenameLock);
		m_fileName = f;
	}
	void SetCwData(DTypePtr cd)
	{
		mCwDataList.clear();
		m_CWData = cd;
	}
	DTypePtr GetCwData() const
	{
		return m_CWData;
	}
	void ClearCwData();
	BOOL IsCwSystemSymbol() const
	{
		return m_CWData && m_CWData->IsSystemSymbol();
	}
	bool HasScopeSuggestions()
	{
		return m_ScopeSuggestions.GetLength() > 0;
	}
	bool HasUe4SuggestionMode() const
	{
		return mScopeSuggestionMode >= smUe4attrFirst && mScopeSuggestionMode <= smUe4attrLast;
	}
	uint GetSymType()
	{
		return m_type;
	}

  protected:
	uint m_type;
	uint mTypeAttrs;
	uint mFormatAttrs;
	bool m_isSysFile = false;
	ParseType m_parseType = ParseType_NotSet;
	LineMarkersPtr mGotoMarkers;
	bool m_parseAll;
	int m_ftype; // java /cpp
	BOOL m_writeToDFile = FALSE;
	UINT m_fileId = 0;

  private:
	CCriticalSection mFilenameLock;
	CStringW m_fileName;
	DTypePtr m_CWData;
};
