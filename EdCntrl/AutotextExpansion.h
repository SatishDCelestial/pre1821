#pragma once

#ifndef AutotextExpansion_h__
#define AutotextExpansion_h__

#include "VSIP\8.0\VisualStudioIntegration\Common\Inc\textmgr2.h"

#include <docobj.h>
#include "VaService.h"
#include "AutotextManager.h"
#include "EDCNT.H"
#include "UndoContext.h"

// This value is non-nullptr only during active expansion session.
extern IVsExpansionSession* gExpSession;

enum KeyWords
{
	kw_none = 0x0,
	kw_end = 0x1,
	kw_selected = 0x2,
	kw_dollar = 0x4,
	kw_other = 0x8,
	kw_default = kw_end | kw_dollar
};

enum class IndentingMode
{
	None,         // no indenting nor formatting nor trimming
	VASnippets,   // preprocessed according to VA Snippets rules
	SmartReformat // VA rules first, then reformatted in editor by IDE
};

struct CAutotextPart;
typedef std::shared_ptr<CAutotextPart> CAutotextPartPtr;
typedef std::weak_ptr<CAutotextPart> CAutotextPartWeakPtr;
typedef std::vector<CAutotextPartPtr> IAutotextPartPtrVec;
typedef std::vector<CAutotextPartWeakPtr> IAutotextPartWeakPtrVec;
typedef std::shared_ptr<IAutotextPartWeakPtrVec> CAutotextPartWeakPtrVecPtr;

//*****************************************************************************
//*****************************************************************************

class AutotextFieldFunc : public IVsExpansionFunction
{
  public:
	AutotextFmt::Casing m_casing;
	LONG m_refs;
	_bstr_t m_field_name;
	_bstr_t m_default_value;

	AutotextFieldFunc(AutotextFmt::Casing casing, _bstr_t field_name, _bstr_t default_value);

	virtual ~AutotextFieldFunc()
	{
	}

	static AutotextFieldFunc* CreateInstance(std::vector<_bstr_t>& params);

	/////////////////////////////
	// IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();

	/////////////////////////////
	// IVsExpansionFunction
	virtual HRESULT STDMETHODCALLTYPE GetFunctionType(ExpansionFunctionType* pFuncType);
	virtual HRESULT STDMETHODCALLTYPE GetListCount(long* iCount);
	virtual HRESULT STDMETHODCALLTYPE GetListText(long iIndex, BSTR* pbstrText);
	virtual HRESULT STDMETHODCALLTYPE GetDefaultValue(
	    /* [out] */ BSTR* bstrValue,
	    /* [out] */ BOOL* fHasDefaultValue);
	virtual HRESULT STDMETHODCALLTYPE FieldChanged(
	    /* [in] */ BSTR bstrField,
	    /* [out] */ BOOL* fRequeryFunction);
	virtual HRESULT STDMETHODCALLTYPE GetCurrentValue(
	    /* [out] */ BSTR* bstrValue,
	    /* [out] */ BOOL* fHasCurrentValue);
	virtual HRESULT STDMETHODCALLTYPE ReleaseFunction(void);
};

//*****************************************************************************
//*****************************************************************************

class AutotextExpansionFields
{
	std::vector<std::pair<_bstr_t, std::map<_bstr_t, _bstr_t>>> m_fields;
	static bool SpanContains(const TextSpan& span, int line, int col);
	static HRESULT FindDeclNode(IXMLDOMNode* src, CComPtr<IXMLDOMNode>& decl_node);

  public:
	AutotextExpansionFields();
	virtual ~AutotextExpansionFields();

	// ***********************************************************
	// WARNING!!!
	// ***********************************************************
	// DON'T use GetDeclarationNode method of IVsExpansionSession
	// object as it does something strange and IDE is not able to
	// close safely. It was throwing Access Violation exceptions
	// and call stack stopped at WaitForSingleObject call in IDE.
	// I used only GetDeclarationNode with first argument as NULL.

	void Init(IXMLDOMNode* node);
	void Clear();
	int FindFieldInSession(IVsExpansionSession* ex_session, const TextSpan& sel_span);
	int FindCurrentFieldInSession(IVsTextView* m_view, IVsExpansionSession* ex_session);

	size_t Count() const;
	_bstr_t GetFieldName(size_t index);
	_bstr_t GetFieldProperty(size_t index, _bstr_t prop_name);
};

class AutotextExpansionClient : public IVsExpansionClient, public IOleCommandTarget
{
	CComPtr<IVsTextView> m_view;
	CComPtr<IOleCommandTarget> m_next_cmd_filter;
	LONG m_refs;
	bool m_is_cmd_filter;
	AutotextExpansionFields m_fields;
	bool m_saved_extend_comments;
	std::function<void()> m_cleanup;
	IndentingMode m_indent_mode;
	int m_file_type;
	CStringW m_line_break;

  public:
	static bool InsertExpansion(EdCntPtr ed, IXMLDOMNode* pSnippet, const GUID& guidLang, IndentingMode indent,
	                            std::function<void()> cleanup = nullptr);

	AutotextExpansionClient(IVsTextView* view, int ftype, IndentingMode indent, LPCWSTR line_break,
	                        std::function<void()> cleanup);
	virtual ~AutotextExpansionClient();

	static void EncodeFuncParamInPlace(CStringW& s);
	static CStringW EncodeFuncParam(const CStringW& s);
	static CStringW ProtectionStr(int ftype);

	/////////////////////////////
	// IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject);
	virtual ULONG STDMETHODCALLTYPE AddRef();
	virtual ULONG STDMETHODCALLTYPE Release();

	/////////////////////////////
	// IVsExpansionClient
	virtual HRESULT STDMETHODCALLTYPE GetExpansionFunction(
	    /* [in] */ IXMLDOMNode* xmlFunctionNode,
	    /* [in] */ BSTR bstrFieldName,
	    /* [out] */ IVsExpansionFunction** pFunc);

	virtual HRESULT STDMETHODCALLTYPE FormatSpan(IVsTextLines* pBuffer, TextSpan* ts);
	virtual HRESULT STDMETHODCALLTYPE EndExpansion(void);
	virtual HRESULT STDMETHODCALLTYPE IsValidType(
	    /* [in] */ IVsTextLines* pBuffer,
	    /* [in] */ TextSpan* ts,
	    /* [size_is][in] */ BSTR* rgTypes,
	    /* [in] */ int iCountTypes,
	    /* [out] */ BOOL* pfIsValidType);
	virtual HRESULT STDMETHODCALLTYPE IsValidKind(
	    /* [in] */ IVsTextLines* pBuffer,
	    /* [in] */ TextSpan* ts,
	    /* [in] */ BSTR bstrKind,
	    /* [out] */ BOOL* pfIsValidKind);

	virtual HRESULT STDMETHODCALLTYPE OnBeforeInsertion(IVsExpansionSession* pSession);
	virtual HRESULT STDMETHODCALLTYPE OnAfterInsertion(IVsExpansionSession* pSession);
	virtual HRESULT STDMETHODCALLTYPE PositionCaretForEditing(IVsTextLines* pBuffer, TextSpan* ts);
	virtual HRESULT STDMETHODCALLTYPE OnItemChosen(BSTR pszTitle, BSTR pszPath);

	/////////////////////////////
	// IOleCommandTarget
	virtual /* [input_sync] */ HRESULT STDMETHODCALLTYPE QueryStatus(
	    /* [unique][in] */ __RPC__in_opt const GUID* pguidCmdGroup,
	    /* [in] */ ULONG cCmds,
	    /* [out][in][size_is] */ __RPC__inout_ecount_full(cCmds) OLECMD prgCmds[],
	    /* [unique][out][in] */ __RPC__inout_opt OLECMDTEXT* pCmdText);

	virtual HRESULT STDMETHODCALLTYPE Exec(
	    /* [unique][in] */ __RPC__in_opt const GUID* pguidCmdGroup,
	    /* [in] */ DWORD nCmdID,
	    /* [in] */ DWORD nCmdexecopt,
	    /* [unique][in] */ __RPC__in_opt VARIANT* pvaIn,
	    /* [unique][out][in] */ __RPC__inout_opt VARIANT* pvaOut);
};

//*****************************************************************************
//*****************************************************************************

/////////////////////////////////////////////////////////
// Base class for parts
//

struct CAutotextPart
{
	enum Type
	{
		Undefined = 0x00,
		Code = 0x01,
		Keyword = 0x02,
		UserKeyword = 0x04,
	};

	CAutotextPartWeakPtrVecPtr group;
	mutable CAutotextPartWeakPtr prev;
	mutable CAutotextPartWeakPtr next;

	std::shared_ptr<GUID> guid;
	std::shared_ptr<SYSTEMTIME> time;

	unsigned int occurrence = 0;

	virtual ~CAutotextPart();

	CAutotextPartPtr GetNext() const
	{
		if (next.expired())
			return nullptr;
		else
			return next.lock();
	}

	CAutotextPartPtr GetPrev() const
	{
		if (prev.expired())
			return nullptr;
		else
			return prev.lock();
	}

	virtual Type GetType() const = 0;

	virtual bool HasName() const
	{
		return false;
	}
	virtual bool HasModifier() const
	{
		return false;
	}
	virtual bool HasDefaultValue() const
	{
		return false;
	}
	virtual bool HasValue() const
	{
		return false;
	}

	virtual bool GetName(CStringW& name) const
	{
		return false;
	}
	virtual bool GetModifier(CStringW& name) const
	{
		return false;
	}
	virtual bool GetDefaultValue(CStringW& val) const
	{
		return false;
	}
	virtual bool GetValue(CStringW& val) const
	{
		return false;
	}
	virtual bool IsEditable() const
	{
		return false;
	}

	virtual bool SetDefaultValue(const CStringW& val)
	{
		return false;
	}
	virtual bool SetName(const CStringW& val)
	{
		return false;
	}
	virtual bool SetModifier(const CStringW& name)
	{
		return false;
	}
	virtual bool SetValue(const CStringW& val)
	{
		return false;
	}

	bool IsOfType(Type mask) const
	{
		return (GetType() & mask) == mask;
	}
	bool IsOfType(DWORD mask) const
	{
		return (GetType() & mask) == mask;
	}

	bool IsKeyword() const
	{
		return IsOfType(Keyword);
	}
	bool IsCode() const
	{
		return IsOfType(Code);
	}
	bool IsUserKeyword() const
	{
		return IsOfType(UserKeyword);
	}

	bool NameIs(LPCWSTR name) const;
	bool ModifierIs(LPCWSTR modif) const;
	bool AppendExpanded(CStringW& buff, bool modifier = false, bool default_val = false) const;
	bool AppendExpandedEx(CStringW& buff, bool $_to_$$, bool va_kw, bool env_var, bool modifier = false,
	                      bool default_val = false) const;
	void AppendDeclarationTag(CStringW& xml_buff) const;
	bool AppendCollapsed(CStringW& buff, bool modifier = false, bool default_val = false) const;
	bool GetExpanded(CStringW& val, bool modifier = false, bool default_val = false) const;
	bool GetExpandedEx(CStringW& val, bool $_to_$$, bool va_kw, bool env_var, bool modifier = false,
	                   bool default_val = false) const;
	bool GetCollapsed(CStringW& val, bool modifier = false, bool default_val = false) const;
};

/////////////////////////////////////////////////////////
// Represents a normal code or text w/o keywords
// This class always return same value as it has no
// difference between collapsed and expanded state.

class CAutotextPartCode : public CAutotextPart
{
  protected:
	CStringW m_code;

  public:
	CAutotextPartCode(const CStringW& code) : m_code(code)
	{
	}

	virtual ~CAutotextPartCode()
	{
	}

	virtual Type GetType() const
	{
		return CAutotextPart::Code;
	}

	virtual bool SetDefaultValue(const CStringW& val)
	{
		return SetValue(val);
	}
	virtual bool GetDefaultValue(CStringW& val) const
	{
		return GetValue(val);
	}
	virtual bool HasDefaultValue() const
	{
		return HasValue();
	}

	virtual bool SetValue(const CStringW& val)
	{
		m_code = val;
		return true;
	}
	virtual bool GetValue(CStringW& val) const
	{
		val = m_code;
		return true;
	}
	virtual bool HasValue() const
	{
		return !m_code.IsEmpty();
	}

	virtual bool AppendCollapsed(CStringW& val) const
	{
		val += m_code;
		return true;
	}
	virtual bool AppendExpanded(CStringW& val) const
	{
		val += m_code;
		return true;
	}
};

/////////////////////////////////////////////////////////
// Represents a Reserved String keyword
//

class CAutotextPartKeyword : public CAutotextPart
{
  protected:
	CStringW m_name;
	CStringW m_modif;
	mutable CStringW m_value;

  public:
	CAutotextPartKeyword(const CStringW& name, const CStringW& modifier = CStringW()) : m_name(name), m_modif(modifier)
	{
	}

	virtual ~CAutotextPartKeyword()
	{
	}

	virtual Type GetType() const
	{
		return CAutotextPart::Keyword;
	}

	virtual bool HasName() const
	{
		return !m_name.IsEmpty();
	}
	virtual bool SetName(const CStringW& name)
	{
		m_name = name;
		return true;
	}
	virtual bool GetName(CStringW& name) const
	{
		name = m_name;
		return true;
	}

	virtual bool HasModifier() const
	{
		return !m_modif.IsEmpty();
	}
	virtual bool SetModifier(const CStringW& name)
	{
		m_modif = name;
		return true;
	}
	virtual bool GetModifier(CStringW& name) const
	{
		name = m_modif;
		return true;
	}

	virtual bool HasDefaultValue() const
	{
		return HasValue();
	}
	virtual bool SetDefaultValue(const CStringW& val)
	{
		return SetValue(val);
	}
	virtual bool GetDefaultValue(CStringW& val) const
	{
		if (!m_value.IsEmpty())
		{
			val = m_value;
			return true;
		}
		return false;
	}

	virtual bool HasValue() const
	{
		if (!m_value.IsEmpty())
			return true;

		CStringW full_keyword;
		if (GetCollapsed(full_keyword, true, true))
		{
			WTString value(full_keyword);
			AutotextManager::ExpandReservedString(g_currentEdCnt, value, guid.get(), time.get());
			m_value = (LPCWSTR)value.Wide();

			// Unresolved keyword alert!
			_ASSERTE(m_value != full_keyword);

			return true;
		}

		return false;
	}

	virtual bool SetValue(const CStringW& val)
	{
		m_value = val;
		return true;
	}
	virtual bool GetValue(CStringW& val) const
	{
		if (HasValue())
		{
			val = m_value;
			return true;
		}

		return false;
	}
};

/////////////////////////////////////////////////////////
// Represents a $$ keyword
//

class CAutotextPartDollar : public CAutotextPartKeyword
{
  public:
	CAutotextPartDollar() : CAutotextPartKeyword(L"")
	{
	}
	virtual ~CAutotextPartDollar()
	{
	}

	virtual bool HasValue() const
	{
		return true;
	}
	virtual bool GetValue(CStringW& val) const
	{
		val = L"$";
		return true;
	}

	virtual bool HasName() const
	{
		return true;
	}
	virtual bool GetName(CStringW& val) const
	{
		val = L"";
		return true;
	}
};

/////////////////////////////////////////////////////////
// Represents a $selected$ keyword
//

class CAutotextPartSelected : public CAutotextPartKeyword
{
  protected:
	mutable bool m_has_value = false;

  public:
	CAutotextPartSelected() : CAutotextPartKeyword(L"selected")
	{
	}
	virtual ~CAutotextPartSelected()
	{
	}

	virtual bool HasValue() const
	{
		if (m_has_value)
			return true;

		m_value = g_currentEdCnt->GetSelStringW();

		auto next_part = GetNext();
		if (next_part) // if not last in row
		{
			int len = m_value.GetLength();
			if (len)
			{
				WCHAR last = m_value[len - 1];
				WCHAR prev2 = len > 1 ? m_value[len - 2] : (WCHAR)0;

				int trim_len = 0;
				if (last == '\n' && prev2 == '\r')
					trim_len = 2;
				else if ((last == '\n' || last == '\r') && prev2 != last)
					trim_len = 1;

				if (trim_len)
				{
					m_value = m_value.Mid(0, len - trim_len);
				}
			}
		}

		return m_has_value = true;
	}
};

/////////////////////////////////////////////////////////
// Represents a User keyword
//

class CAutotextPartUserKW : public CAutotextPartKeyword
{
	CStringW m_default_val;
	bool m_editable;

  public:
	CAutotextPartUserKW(const CStringW& name, const CStringW& modifier = L"", const CStringW& default_value = L"",
	                    bool editable = true)
	    : CAutotextPartKeyword(name, modifier), m_default_val(default_value), m_editable(editable)
	{
	}

	virtual ~CAutotextPartUserKW()
	{
	}

	virtual Type GetType() const
	{
		return CAutotextPart::UserKeyword;
	}

	virtual bool GetValue(CStringW& val) const
	{
		if (!m_value.IsEmpty())
		{
			val = m_value;
			return true;
		}
		return false;
	}

	virtual bool HasDefaultValue() const
	{
		return !m_default_val.IsEmpty();
	}
	virtual bool SetDefaultValue(const CStringW& val)
	{
		m_default_val = val;
		m_default_val.Trim();
		return true;
	}
	virtual bool GetDefaultValue(CStringW& val) const
	{
		if (!m_default_val.IsEmpty())
		{
			val = m_default_val;
			return true;
		}
		return false;
	}

	virtual bool IsEditable() const
	{
		return m_editable;
	}
};

std::function<void(CStringW&)> GetModifierFunc(const WTString& name);

const std::wregex& GetKeywordsRegex();

size_t GetAutotextParts(const CStringW& snippet, std::vector<CAutotextPartPtr>& parts, bool whole_if_no_match = false);
CStringW EscapeXMLSpecialChars(const CStringW& input);
CStringW WrapInCDATA(const CStringW& input);

CStringW CreateIndentString(long num_columns);  // creates indent string consisting of tabs and spaces
CStringW GetIndentString(const CStringW& line); // returns existing indent in line
long FindIndentPosition(const CStringW& line);  // returns a number columns in indent
long FindIndentPositionForIndex(const CStringW& line, long index); // returns a number columns in indent

bool InsertVsSnippetXML(EdCntPtr ed, LPCWSTR vsSnippXML, IndentingMode indenting);
void ForEachEnvVariable(std::function<bool(LPCWSTR, // name
                                           int,     // name length
                                           LPCWSTR, // value
                                           int      // value length
                                           )>
                            process_var);

void ExpandVASpecificKeywords(CStringW& snipp, KeyWords preserve_kw, LPCWSTR dollarSubstitution, const GUID* guid,
                              const SYSTEMTIME* time);
void ExpandEnvironmentStringsEx(CStringW& in_out_str, LPCWSTR dollarSubstitution);
bool ConvertAutotextPartsToVsSnippetXML(CStringW& out_vs_snipp_XML, std::vector<CAutotextPartPtr>& parts,
                                        LPCWSTR selection, LPCWSTR clipboard, KeyWords preserve_kws = kw_default);
bool ConvertVaSnippetToVsSnippetXML(EdCntPtr ed, CStringW& out_vs_snipp_XML, const CStringW& in_va_snipp,
                                    IndentingMode indent, KeyWords preserve_kws = kw_default);

HRESULT FindXMLNode(IXMLDOMNode* in_root, const _bstr_t& in_node_name, CComPtr<IXMLDOMNode>& out_node);
bool GetXMLNodeText(IXMLDOMNode* in_root, const _bstr_t& in_node_name, _bstr_t& out_text);
bool GetXMLNodeTextLines(IXMLDOMNode* in_root, const _bstr_t& in_node_name, std::vector<CStringW>& lines);

#endif // AutotextExpansion_h__
