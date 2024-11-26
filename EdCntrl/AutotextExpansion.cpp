#include "stdafxed.h"
#include "AutotextExpansion.h"
#include "Settings.h"
#include "IdeSettings.h"
#include "PROJECT.H"
#include "VaTimers.h"
#include "VAAutomation.h"
#include "WindowUtils.h"
#include "VaMessages.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

IVsExpansionSession* gExpSession = nullptr;

AutotextFieldFunc::AutotextFieldFunc(AutotextFmt::Casing casing, _bstr_t field_name, _bstr_t default_value)
    : m_casing(casing), m_refs(0), m_field_name(field_name), m_default_value(default_value)
{
}

AutotextFieldFunc* AutotextFieldFunc::CreateInstance(std::vector<_bstr_t>& params)
{
	switch (params.size())
	{
	case 2:
		return new AutotextFieldFunc(AutotextFmt::ParseCasingStr(params[0]), params[1], _bstr_t());
	case 3:
		return new AutotextFieldFunc(AutotextFmt::ParseCasingStr(params[0]), params[1], params[2]);
	}
	return nullptr;
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::QueryInterface(REFIID riid, void** ppvObject)
{
	if (riid == IID_IUnknown || riid == IID_IVsExpansionFunction)
	{
		*ppvObject = static_cast<IVsExpansionFunction*>(this);
		AddRef();
		return S_OK;
	}

	*ppvObject = nullptr;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE AutotextFieldFunc::AddRef()
{
	return (ULONG)InterlockedIncrement(&m_refs);
}

ULONG STDMETHODCALLTYPE AutotextFieldFunc::Release()
{
	const LONG cRef = InterlockedDecrement(&m_refs);
	if (cRef == 0)
	{
		delete this;
	}
	return (ULONG)cRef;
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::GetFunctionType(ExpansionFunctionType* pFuncType)
{
	*pFuncType = eft_Value;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::GetListCount(long* iCount)
{
	*iCount = 0;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::GetListText(long iIndex, BSTR* pbstrText)
{
	*pbstrText = nullptr;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::GetDefaultValue(/* [out] */ BSTR* bstrValue,
                                                             /* [out] */ BOOL* fHasDefaultValue)
{
	if (m_default_value.GetBSTR() == nullptr)
	{
		*bstrValue = nullptr;
		*fHasDefaultValue = FALSE;
		return S_OK;
	}

	CStringW val((LPCWSTR)m_default_value);
	AutotextFmt::ApplyCasing(m_casing, val);
	*bstrValue = val.AllocSysString();
	*fHasDefaultValue = TRUE;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::FieldChanged(/* [in] */ BSTR bstrField, /* [out] */ BOOL* fRequeryFunction)
{
	*fRequeryFunction = m_field_name == _bstr_t(bstrField);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::GetCurrentValue(/* [out] */ BSTR* bstrValue,
                                                             /* [out] */ BOOL* fHasCurrentValue)
{
	if (gExpSession)
	{
		BSTR field_val = nullptr;
		HRESULT hr = E_FAIL;
		if (SUCCEEDED(hr = gExpSession->GetFieldValue(m_field_name, &field_val)))
		{
			CStringW val((LPCWSTR)_bstr_t(field_val, false));
			AutotextFmt::ApplyCasing(m_casing, val);
			*bstrValue = val.AllocSysString();
			*fHasCurrentValue = TRUE;
		}
		return hr;
	}
	return GetDefaultValue(bstrValue, fHasCurrentValue);
}

HRESULT STDMETHODCALLTYPE AutotextFieldFunc::ReleaseFunction(void)
{
	return S_OK;
}

bool AutotextExpansionFields::SpanContains(const TextSpan& span, int line, int col)
{
	if (line > span.iStartLine && line < span.iEndLine)
		return true;
	else if (line == span.iStartLine && span.iStartLine == span.iEndLine)
		return col >= span.iStartIndex && col <= span.iEndIndex;
	else if (line == span.iStartLine)
		return col >= span.iStartIndex;
	else if (line == span.iEndLine)
		return col <= span.iEndIndex;

	return false;
}

HRESULT AutotextExpansionFields::FindDeclNode(IXMLDOMNode* src, CComPtr<IXMLDOMNode>& decl_node)
{
	_bstr_t decl_name(L"Declarations");
	return FindXMLNode(src, decl_name, decl_node);
}

AutotextExpansionFields::AutotextExpansionFields()
{
}

AutotextExpansionFields::~AutotextExpansionFields()
{
}

void AutotextExpansionFields::Init(IXMLDOMNode* node)
{
	CComPtr<IXMLDOMNode> decls;
	CComPtr<IXMLDOMNodeList> node_list;
	long node_list_length = 0;
	if (SUCCEEDED(FindDeclNode(node, decls)) && decls && SUCCEEDED(decls->get_childNodes(&node_list)) && node_list &&
	    SUCCEEDED(node_list->get_length(&node_list_length)))
	{
		for (long i = 0; i < node_list_length; i++)
		{
			CComPtr<IXMLDOMNode> child;
			CComPtr<IXMLDOMNodeList> props;
			long props_length;
			if (SUCCEEDED(node_list->get_item(i, &child)) && child && SUCCEEDED(child->get_childNodes(&props)) &&
			    props && SUCCEEDED(props->get_length(&props_length)))
			{
				std::map<_bstr_t, _bstr_t> data;

				for (long p = 0; p < props_length; p++)
				{
					CComPtr<IXMLDOMNode> prop;
					if (SUCCEEDED(props->get_item(p, &prop)))
					{
						_bstr_t tag_name, tag_text;
						if (SUCCEEDED(prop->get_nodeName(tag_name.GetAddress())) &&
						    SUCCEEDED(prop->get_text(tag_text.GetAddress())))
						{
							data[tag_name] = tag_text;
						}
					}
				}

				static _bstr_t id_name(L"ID");

				auto id_it = data.find(id_name);
				if (id_it != data.end())
				{
					_bstr_t id = id_it->second;
					data.erase(id_it);
					m_fields.push_back(std::make_pair(id, data));
				}
			}
		}
	}
}

void AutotextExpansionFields::Clear()
{
	m_fields.clear();
}

int AutotextExpansionFields::FindFieldInSession(IVsExpansionSession* ex_session, const TextSpan& sel_span)
{
	for (size_t i = 0; i < m_fields.size(); i++)
	{
		TextSpan span;
		if (SUCCEEDED(ex_session->GetFieldSpan(m_fields[i].first, &span)))
		{
			if (SpanContains(span, sel_span.iStartLine, sel_span.iStartIndex))
			{
				return (int)i;
			}
		}
	}

	return -1;
}

int AutotextExpansionFields::FindCurrentFieldInSession(IVsTextView* m_view, IVsExpansionSession* ex_session)
{
	TextSpan sel_span;
	if (gExpSession && m_view && SUCCEEDED(m_view->GetSelectionSpan(&sel_span)))
	{
		return FindFieldInSession(ex_session, sel_span);
	}
	return -1;
}

size_t AutotextExpansionFields::Count() const
{
	return m_fields.size();
}

_bstr_t AutotextExpansionFields::GetFieldName(size_t index)
{
	return m_fields[index].first;
}

_bstr_t AutotextExpansionFields::GetFieldProperty(size_t index, _bstr_t prop_name)
{
	auto it2 = m_fields[index].second.find(prop_name);
	if (it2 != m_fields[index].second.end())
		return it2->second;

	return _bstr_t();
}

bool AutotextExpansionClient::InsertExpansion(EdCntPtr ed, IXMLDOMNode* pSnippet, const GUID& guidLang,
                                              IndentingMode indent, std::function<void()> cleanup /*= nullptr*/)
{
	if (!ed || !ed->m_IVsTextView || !pSnippet)
	{
		_ASSERTE(FALSE);
		return false;
	}

	CComPtr<IVsTextLines> pBuffer;
	ed->m_IVsTextView->GetBuffer(&pBuffer);
	if (pBuffer)
	{
		// get IVsExpansion using query interface

		CComQIPtr<IVsExpansion> vsExpP(pBuffer);

		if (vsExpP)
		{
			TextSpan span;
			if (SUCCEEDED(ed->m_IVsTextView->GetSelectionSpan(&span)))
			{
				_bstr_t type_text;
				if (GetXMLNodeText(pSnippet, _bstr_t(L"SnippetType"), type_text))
				{
					if (wcsstr((LPCWSTR)type_text, L"SurroundsWith"))
					{
						span.iEndLine = span.iStartLine;
						span.iEndIndex = span.iStartIndex;
					}
				}

				if (gExpSession)
					gExpSession->EndCurrentExpansion(TRUE);

				// check if previous session is ended already
				_ASSERTE(gExpSession == nullptr);
				gExpSession = nullptr;

				CStringW lnBrk = ed->GetLineBreakString().Wide();
				CComPtr<IVsExpansionClient> client(
				    new AutotextExpansionClient(ed->m_IVsTextView, gTypingDevLang, indent, lnBrk, cleanup));
				CComPtr<IVsExpansionSession> session;

				if (SUCCEEDED(vsExpP->InsertSpecificExpansion(pSnippet, span, client, guidLang, nullptr, &session)))
				{
					return true;
				}
			}
		}
	}

	return false;
}

AutotextExpansionClient::AutotextExpansionClient(IVsTextView* view, int ftype, IndentingMode indent, LPCWSTR line_break,
                                                 std::function<void()> cleanup)
    : m_view(view), m_refs(0), m_is_cmd_filter(false) /*, m_saved_pretty_listing()*/
      ,
      m_saved_extend_comments(Psettings->mExtendCommentOnNewline), m_cleanup(cleanup), m_indent_mode(indent),
      m_file_type(ftype), m_line_break(line_break ? line_break : L"\r\n")
{
	// m_fields.Init(snippet);
}

AutotextExpansionClient::~AutotextExpansionClient()
{
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::QueryInterface(REFIID riid, void** ppvObject)
{
	if (!ppvObject)
		return E_POINTER;

	if (riid == IID_IUnknown || riid == IID_IVsExpansionClient)
	{
		*ppvObject = static_cast<IVsExpansionClient*>(this);
		AddRef();
		return S_OK;
	}
	else if (riid == IID_IOleCommandTarget)
	{
		*ppvObject = static_cast<IOleCommandTarget*>(this);
		AddRef();
		return S_OK;
	}

	*ppvObject = nullptr;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE AutotextExpansionClient::AddRef()
{
	return (ULONG)InterlockedIncrement(&m_refs);
}

ULONG STDMETHODCALLTYPE AutotextExpansionClient::Release()
{
	const LONG cRef = InterlockedDecrement(&m_refs);

	if (cRef == 0)
		delete this;

	return (ULONG)cRef;
}

void AutotextExpansionClient::EncodeFuncParamInPlace(CStringW& s)
{
	CStringW rslt;
	for (int i = 0; i < s.GetLength(); i++)
	{
		WCHAR ch = s[i];
		if (ch == '\\' || ch == '(' || ch == ',' || ch == ')')
			rslt.AppendChar('\\');
		rslt.AppendChar(ch);
	}
	s = rslt;
}

CStringW AutotextExpansionClient::EncodeFuncParam(const CStringW& s)
{
	CStringW rslt = s;
	EncodeFuncParamInPlace(rslt);
	return rslt;
}

CStringW AutotextExpansionClient::ProtectionStr(int ftype)
{
	if (Is_Tag_Based(ftype))
		return L"<!---->"; // XML,XAML,ASP,HTML
	if (Is_VB_VBS_File(ftype))
		return L"'"; // VB,VBS
	else
		return L"/**/"; // C#,C++ (and other)
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::GetExpansionFunction(/* [in] */ IXMLDOMNode* xmlFunctionNode,
                                                                        /* [in] */ BSTR bstrFieldName,
                                                                        /* [out] */ IVsExpansionFunction** pFunc)
{
	// A code snippet can specify the name of an expansion function that is "called" to supply a value that is displayed
	// in a code snippet field. This expansion function is represented by the IVsExpansionFunction interface and calling
	// the expansion function means calling the GetCurrentValue method on that interface.

	// The XML node contains the code snippet's expansion function tag (see Function Element (IntelliSense Code
	// Snippets) for details). It is up to the implementation of the GetExpansionFunction method to parse the expansion
	// function text for its name and any parameters it may require.

	if (!pFunc)
		return E_POINTER;

	*pFunc = nullptr;

	if (!xmlFunctionNode)
		return E_POINTER;

	_bstr_t node_text;
	if (SUCCEEDED(xmlFunctionNode->get_text(node_text.GetAddress())))
	{
		LPCWSTR txt = node_text;
		size_t txt_len = node_text.length();

		CStringW fnc_name;
		std::vector<_bstr_t> params;

		CStringW token;
		for (size_t i = 0; i < txt_len; i++)
		{
			switch (txt[i])
			{
			case '(':
				fnc_name = token;
				token.Empty();
				break;
			case ',':
			case ')':
				params.push_back(_bstr_t(token.AllocSysString(), false));
				token.Empty();
				break;
			case '\\':
				if (++i < txt_len)
					token += txt[i];
				break;
			default:
				token += txt[i];
			}
		}

		if (fnc_name == L"VaFieldFunc")
			*pFunc = AutotextFieldFunc::CreateInstance(params);
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::FormatSpan(IVsTextLines* pBuffer, TextSpan* ts)
{
	// The specified span describes the extent of the code snippet after it has been inserted.
	// Formatting typically involves inserting tabs or spaces to position the snippet relative to the code around the
	// snippet.

	_ASSERTE(gPkgServiceProvider);

	if (!pBuffer || !ts || !gExpSession)
		return E_POINTER;

	auto getLineText = [&](long lin) -> CStringW {
		long lin_len;
		_bstr_t lin_text;

		if (SUCCEEDED(pBuffer->GetLengthOfLine(lin, &lin_len)) &&
		    SUCCEEDED(pBuffer->GetLineText(lin, 0, lin, lin_len, lin_text.GetAddress())))
		{
			return (LPCWSTR)lin_text;
		}

		return CStringW();
	};

	CStringW protect_str = ProtectionStr(m_file_type);

	////////////////////////////
	// add $end$ protection

	TextSpan end_span;
	bool end_already_protected = false;

	if (SUCCEEDED(gExpSession->GetEndSpan(&end_span)))
	{
		long line_length;
		if (SUCCEEDED(pBuffer->GetLengthOfLine(end_span.iStartLine, &line_length)))
		{
			if (end_span.iStartIndex < line_length)
			{
				_bstr_t end_text;
				if (SUCCEEDED(pBuffer->GetLineText(end_span.iStartLine, end_span.iStartIndex, end_span.iStartLine,
				                                   end_span.iStartIndex + 1, end_text.GetAddress())))
				{
					if (end_text.length() > 0 && !wt_isspace(*((LPCWSTR)end_text)))
						end_already_protected = true;
				}
			}
		}

		if (!end_already_protected &&
		    SUCCEEDED(pBuffer->ReplaceLines(end_span.iStartLine, end_span.iStartIndex, end_span.iStartLine,
		                                    end_span.iStartIndex, protect_str, protect_str.GetLength(), nullptr)))
		{
			gExpSession->SetEndSpan(end_span);
		}
	}

	////////////////////////////
	// alternate default '\n'
	// apply new line breaks from bottom to top...

	if (m_line_break.Compare(L"\n") != 0)
	{
		TextSpan span;
		span.iEndLine = ts->iEndLine;
		while (span.iEndLine >= ts->iStartLine)
		{
			span.iStartLine = std::max(ts->iStartLine, span.iEndLine - 1000);

			if (span.iStartLine == ts->iStartLine)
				span.iStartIndex = ts->iStartIndex;
			else
				span.iStartIndex = 0;

			if (span.iEndLine == ts->iEndLine)
				span.iEndIndex = ts->iEndIndex;
			else
			{
				if (FAILED(pBuffer->GetLengthOfLine(span.iEndLine, &span.iEndIndex)))
					break;
			}

			_bstr_t str;
			if (SUCCEEDED(pBuffer->GetLineText(span.iStartLine, span.iStartIndex, span.iEndLine, span.iEndIndex,
			                                   str.GetAddress())))
			{
				CStringW wstr((LPCWSTR)str);
				wstr.Replace(L"\n", m_line_break);
				pBuffer->ReloadLines(span.iStartLine, span.iStartIndex, span.iEndLine, span.iEndIndex, wstr,
				                     wstr.GetLength(), nullptr);
			}

			span.iEndLine = span.iStartLine - 1;
		}
	}

	///////////////////////////////////
	// apply VS indenting (if required)

	if (m_indent_mode == IndentingMode::SmartReformat)
	{
		TextSpan actual_span = *ts;
		if (SUCCEEDED(gExpSession->GetSnippetSpan(&actual_span)) &&
		    SUCCEEDED(pBuffer->GetLengthOfLine(actual_span.iEndLine, &actual_span.iEndIndex)))
		{
			actual_span.iStartIndex = 0;
			bool isFormatted = false;

			if (gPkgServiceProvider)
			{
				GUID guid;
				if (SUCCEEDED(pBuffer->GetLanguageServiceID(&guid)))
				{
					CComPtr<IUnknown> tmp;
					if (SUCCEEDED(gPkgServiceProvider->QueryService(guid, IID_IVsLanguageTextOps, (void**)&tmp)) && tmp)
					{
						CComQIPtr<IVsLanguageTextOps> languageTextOps{tmp};
						CComQIPtr<IVsTextLayer> vsTextLayer{pBuffer};

						if (languageTextOps && vsTextLayer)
						{
							languageTextOps->Format(vsTextLayer, &actual_span);
							isFormatted = true;
						}
					}
				}
			}

			if (!isFormatted)
			{
				// use nasty selection formatting
				// not nice, but works surprisingly well
				if (SUCCEEDED(m_view->SetSelection(actual_span.iStartLine, actual_span.iStartIndex,
				                                   actual_span.iEndLine, actual_span.iEndIndex)))
				{
					gShellSvc->FormatSelection();
				}
			}
		}
	}

	////////////////////////////
	// remove $end$ protection

	if (!end_already_protected && SUCCEEDED(gExpSession->GetEndSpan(&end_span)))
	{
		if (SUCCEEDED(pBuffer->ReplaceLines(end_span.iStartLine, end_span.iStartIndex, end_span.iStartLine,
		                                    end_span.iStartIndex + protect_str.GetLength(), L"", 0, nullptr)))
		{
			gExpSession->SetEndSpan(end_span);
		}
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::EndExpansion(void)
{
	// This method is called when the code snippet has been inserted and the user has completed all changes
	// to the snippet in the special editing phase. At this point, any resources that may have been allocated
	// for inserting or formatting the code snippet can be released.

	gExpSession = nullptr;                                        // end active session
	Psettings->mExtendCommentOnNewline = m_saved_extend_comments; // undo settings change

	// 	if (m_file_type == VB)
	// 		g_IdeSettings->SetEditorOption("Basic-Specific", "PrettyListing", m_saved_pretty_listing ? "TRUE" :
	// "FALSE");

	// release all resources

	m_view->RemoveCommandFilter(this);
	m_is_cmd_filter = false;
	m_next_cmd_filter = nullptr;
	m_fields.Clear();

	if (m_cleanup)
	{
		m_cleanup();
		m_cleanup = nullptr;
	}

	EdCntPtr ed(g_currentEdCnt);
	if (ed)
	{
		// close any lingering tooltips
		ed->DisplayToolTipArgs(false);
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::IsValidType(/* [in] */ IVsTextLines* pBuffer,
                                                               /* [in] */ TextSpan* ts,
                                                               /* [size_is][in] */ BSTR* rgTypes,
                                                               /* [in] */ int iCountTypes,
                                                               /* [out] */ BOOL* pfIsValidType)
{
	if (!pBuffer || !ts || !pfIsValidType)
		return E_POINTER;

	// The rgTypes list contains strings that specify the types of snippets to display.
	// These types can be "Expansion" or "SurroundsWith"
	// (see SnippetType Element (IntelliSense Code Snippets) for details on snippet types).
	// It is possible for a code snippet to not have a type associated with it,
	// in which case the iCountTypes parameter is 0.

	// We are forcing IDE to use our snippet,
	// so it needs to know that it is fine to use it.

	*pfIsValidType = TRUE;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::IsValidKind(/* [in] */ IVsTextLines* pBuffer,
                                                               /* [in] */ TextSpan* ts, /* [in] */ BSTR bstrKind,
                                                               /* [out] */ BOOL* pfIsValidKind)
{
	if (!pBuffer || !ts || !pfIsValidKind)
		return E_POINTER;

	// The bstrKind parameter is a string that specifies the kinds of snippets to display, such as MethodBody, Page, and
	// File. The snippet kind can control in what context the snippet is inserted. For example, a snippet kind of
	// MethodBody should be inserted only in a method. See Code Element (IntelliSense Code Snippets) for a list of all
	// snippet kinds that are supported. It is possible for a code snippet to not have a kind associated with it in
	// which case the bstrKind parameter is an empty string.

	// We are forcing IDE to use our snippet,
	// so it needs to know that it is fine to use it.

	*pfIsValidKind = TRUE;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::OnBeforeInsertion(IVsExpansionSession* pSession)
{
	if (!pSession)
		return E_POINTER;

	// This method is called just before the code snippet is about to be inserted. If the IVsExpansionSession object
	// is given, use it only to gather information about the code snippet that is about to be inserted.
	// The remaining methods are solely for use by the special editing mode.

	gExpSession = pSession;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::OnAfterInsertion(IVsExpansionSession* pSession)
{
	if (!pSession)
		return E_POINTER;

	// This method is called after the code snippet has been inserted and formatted but before the special editing mode
	// for code snippets is enabled. If the IVsExpansionSession object is given, use it only to gather information about
	// the code snippet that was just inserted or to override default field values. The remaining methods are solely for
	// use by the special editing mode.

	if (m_view && m_view->GetWindowHandle())
	{
		m_is_cmd_filter = SUCCEEDED(m_view->AddCommandFilter(this, &m_next_cmd_filter));
	}

	/*

	In case of snippets containing comment,
	VA was extending comment after enter used
	to accept snippet, so it needs to be eliminated

	Example of such snippet:

	// $Prompt$
	// $Prompt=Foo$

	*/

	m_saved_extend_comments = Psettings->mExtendCommentOnNewline;
	Psettings->mExtendCommentOnNewline = false;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::PositionCaretForEditing(IVsTextLines* pBuffer, TextSpan* ts)
{
	if (!pBuffer || !ts)
		return E_POINTER;

	// This method is called after the edit caret has been positioned according to the notations in the code snippet
	// file. This method provides an opportunity to override the normal placement of the edit caret but is rarely used.

	// 	if (m_end_span.iStartLine > 0 && m_end_span.iEndLine > 0
	// 		&& ts
	// 		&&
	// 			(
	// 			ts->iStartLine != m_end_span.iStartLine ||
	// 			ts->iStartIndex != m_end_span.iStartIndex ||
	// 			ts->iEndLine != m_end_span.iEndLine ||
	// 			ts->iEndIndex != m_end_span.iEndIndex
	// 			)
	// 		&&
	// 		!m_end_virtual_indent.IsEmpty()
	// 		&&
	// 		m_end_virtual_indent_start >= 0
	// 		)
	// 	{
	// 		long line_len;
	// 		if (SUCCEEDED(pBuffer->GetLengthOfLine(m_end_span.iStartLine, &line_len))
	// 			&&
	// 			line_len == m_end_virtual_indent_start)
	// 		{
	// 			TextSpan tmp_span;
	//
	// 			pBuffer->ReplaceLines(
	// 				m_end_span.iStartLine, m_end_virtual_indent_start,
	// 				m_end_span.iStartLine, m_end_virtual_indent_start,
	// 				m_end_virtual_indent, m_end_virtual_indent.GetLength(), &tmp_span);
	//
	// 			*ts = m_end_span;
	// 		}
	// 	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::OnItemChosen(BSTR pszTitle, BSTR pszPath)
{
	// This method is called when the user selects a code snippet from a list of snippets,
	// typically presented in an IntelliSense menu. The menu is shown as a result of a call
	// to the InvokeInsertionUI method in the IVsExpansionManager interface.

	// A typical implementation of the OnItemChosen method is to call the InsertNamedExpansion method on the
	// IVsExpansion object that was cached in the IVsExpansionClient object before the InvokeInsertionUI method was
	// called.In fact, the only time the OnItemChosen method is called is when the InvokeInsertionUI method is called,
	// either as a result of the user selecting a menu option that triggers the call or if the user is allowed to choose
	// from multiple code snippets that have the same name(for example, if a code snippet shortcut matches more than one
	// code snippet, a "disambiguation user interface" is shown to allow the user to select which code snippet to
	// actually insert).

	// UNUSED

	return S_OK;
}

bool IsVSStd2KGroup(const GUID* guid)
{
	static const GUID VA_guidVSStd2K = {0x1496A755, 0x94DE, 0x11D0, {0x8C, 0x3F, 0x00, 0xC0, 0x4F, 0xC2, 0xAA, 0xE2}};
	return guid && *guid == VA_guidVSStd2K;
}

/* [input_sync] */ HRESULT STDMETHODCALLTYPE
AutotextExpansionClient::QueryStatus(/* [unique][in] */ __RPC__in_opt const GUID* pguidCmdGroup, /* [in] */ ULONG cCmds,
                                     /* [out][in][size_is] */ __RPC__inout_ecount_full(cCmds) OLECMD prgCmds[],
                                     /* [unique][out][in] */ __RPC__inout_opt OLECMDTEXT* pCmdText)
{
	if (!prgCmds)
		return E_POINTER;

	HRESULT hr = E_UNEXPECTED;

	if (m_next_cmd_filter)
		hr = m_next_cmd_filter->QueryStatus(pguidCmdGroup, cCmds, prgCmds, pCmdText);

	if (hr != S_OK && m_view && m_view->GetWindowHandle() && gExpSession)
	{
		if (IsVSStd2KGroup(pguidCmdGroup))
		{
			for (ULONG i = 0; i < cCmds; i++)
			{
				DWORD nCmdID = prgCmds[i].cmdID;

				if (nCmdID == ECMD_BACKTAB || nCmdID == ECMD_TAB || nCmdID == ECMD_RETURN || nCmdID == ECMD_CANCEL)
				{
					return S_OK;
				}
			}
		}
		else
		{
			return OLECMDERR_E_UNKNOWNGROUP;
		}
	}

	return hr;
}

HRESULT STDMETHODCALLTYPE AutotextExpansionClient::Exec(/* [unique][in] */ __RPC__in_opt const GUID* pguidCmdGroup,
                                                        /* [in] */ DWORD nCmdID, /* [in] */ DWORD nCmdexecopt,
                                                        /* [unique][in] */ __RPC__in_opt VARIANT* pvaIn,
                                                        /* [unique][out][in] */ __RPC__inout_opt VARIANT* pvaOut)
{
	if (m_view && m_view->GetWindowHandle())
	{
		if (IsVSStd2KGroup(pguidCmdGroup) && gExpSession)
		{
			if (nCmdID == ECMD_BACKTAB)
			{
				// GoToPreviousExpansionField()
				// ----------------------------
				// This method is used in support of the special edit mode that is entered after a code snippet is
				// inserted. Typically, this method is called in response to the user typing the Shift-Tab key
				// combination.

				gExpSession->GoToPreviousExpansionField();
				return S_OK;
			}
			else if (nCmdID == ECMD_TAB)
			{
				// GoToNextExpansionField( fCommitIfLast )
				// ---------------------------------------
				// This method is used in support of the special edit mode that is entered after a code snippet is
				// inserted. Typically, this method is called in response to the user typing the Tab key. If you do not
				// wish to support cycling through the fields with the Tab key, set the fCommitIfLast parameter to
				// non-zero (TRUE): this causes all changes made to the code snippet to be committed when attempting to
				// Tab off the last field in the code snippet.

				gExpSession->GoToNextExpansionField(FALSE);
				return S_OK;
			}
			else if (nCmdID == ECMD_RETURN || nCmdID == ECMD_CANCEL)
			{
				// EndCurrentExpansion( fLeaveCaret )
				// ----------------------------------
				// A code snippet template typically marks where the edit caret should be positioned after
				// the snippet has been inserted and the special edit mode is completed. This method is
				// called during the special edit mode when the user types the Enter or ESC keys.
				// If Enter is typed, the fLeaveCaret parameter should be set to zero (FALSE) and
				// if ESC is typed, the fLeaveCaret parameter should be set to non-zero (TRUE).
				// This method positions the caret appropriately in the associated text view before returning.

				// NOTE:
				// On MSDN site the values are swapped as Enter=TRUE and ESC=FALSE, but logically,
				// when user types Enter, he wants to accept snippet, so caret should be moved
				// according to the $end$ position and not left on its current position.

				// CONCLUSION:
				// I tested behavior with built-in snippets in IDE and it does the same as we do.

				gExpSession->EndCurrentExpansion(nCmdID == ECMD_RETURN ? FALSE : TRUE);

				// We could now change the position of caret if we would want.

				return S_OK;
			}
		}
	}

	if (m_next_cmd_filter)
		return m_next_cmd_filter->Exec(pguidCmdGroup, nCmdID, nCmdexecopt, pvaIn, pvaOut);

	return OLECMDERR_E_UNKNOWNGROUP;
}

std::function<void(CStringW&)> GetModifierFunc(const WTString& name)
{
	AutotextFmt::Casing casing = AutotextFmt::Default;

	if (name.MatchRENoCase("_?pascal"))
		casing = AutotextFmt::PascalCase;
	else if (name.MatchRENoCase("_?camel"))
		casing = AutotextFmt::camelCase;
	else if (name.MatchRENoCase("_?upper"))
		casing = AutotextFmt::UPPERCASE;
	else if (name.MatchRENoCase("_?lower"))
		casing = AutotextFmt::lowercase;

	return [casing](CStringW& s) { AutotextFmt::ApplyCasing(casing, s); };
}

CAutotextPart::~CAutotextPart()
{
}

bool CAutotextPart::NameIs(LPCWSTR name) const
{
	if (name && *name)
	{
		CStringW val;
		if (GetName(val))
			return val == name;
	}
	return false;
}

bool CAutotextPart::ModifierIs(LPCWSTR modif) const
{
	if (modif && *modif)
	{
		CStringW val;
		if (GetModifier(val))
			return val == modif;
	}
	return false;
}

bool CAutotextPart::AppendExpanded(CStringW& buff, bool modifier /*= false*/, bool default_val /*= false*/) const
{
	return AppendExpandedEx(buff, false, false, false, modifier, default_val);
}

bool CAutotextPart::AppendExpandedEx(CStringW& buff, bool $_to_$$, bool va_kw, bool env_var, bool modifier /*= false*/,
                                     bool default_val /*= false*/) const
{
#ifdef DEBUG
	if (va_kw && !IsUserKeyword())
		_ASSERTE(!"va_kw is safe only for use with user input");
#endif

	CStringW val;
	bool isExpansion = GetValue(val); // GetValue gives us expanded value with applied modifier!!!
	bool isDefaultVal = !isExpansion && default_val && GetDefaultValue(val);

	if (isExpansion || isDefaultVal)
	{
		if (isDefaultVal && va_kw) // default value of user input may contain dollar keywords
			ExpandVASpecificKeywords(val, kw_default, nullptr, guid.get(), time.get());

		if (env_var)
			ExpandEnvironmentStringsEx(val, nullptr);

		if (modifier)
		{
			CStringW modif_name;
			if (GetModifier(modif_name))
			{
				auto m = GetModifierFunc(modif_name);
				if (m)
					m(val);
			}
		}

		if ($_to_$$)
			val.Replace(L"$", L"$$");

		buff += val;
		return true;
	}
	return false;
}

void CAutotextPart::AppendDeclarationTag(CStringW& xml_buff) const
{
	if (IsUserKeyword())
	{
		CStringW val;
		if (GetName(val))
		{
			if (IsEditable())
				xml_buff.Append(L"\n<Literal>");
			else
				xml_buff.Append(L"\n<Literal Editable='false'>");

			val = EscapeXMLSpecialChars(val);
			CString__AppendFormatW(xml_buff, L"\n\t<ID>%s</ID>", (LPCWSTR)val);

			val.Replace(L'_', L' ');
			CString__AppendFormatW(xml_buff, L"\n\t<ToolTip>%s</ToolTip>", (LPCWSTR)val);

			val.Empty();
			// don't replace $->$$ for default value
			// but replace $name$ and %name% keywords
			if (!GetExpandedEx(val, false /*don't*/, true, true, true, true))
				GetName(val); // use the name as hint

			val = WrapInCDATA(val);
			CString__AppendFormatW(xml_buff, L"\n\t<Default>%s</Default>", (LPCWSTR)val);

			xml_buff.Append(L"\n</Literal>");
		}
	}
}

bool CAutotextPart::AppendCollapsed(CStringW& buff, bool modifier /*= false*/, bool default_val /*= false*/) const
{
	CStringW val;

	if (IsCode() && GetValue(val))
	{
		buff += val;
		return true;
	}
	else if (IsKeyword() && GetName(val))
	{
		buff += L'$';
		buff += val;

		if (modifier && GetModifier(val))
			buff += val;

		buff += L'$';
		return true;
	}
	else if (IsUserKeyword() && GetName(val))
	{
		buff += L'$';
		buff += val;

		if (modifier && GetModifier(val))
			buff += val;

		if (default_val && GetDefaultValue(val))
		{
			buff += L'=';
			buff += val;
		}

		buff += L'$';

		return true;
	}
	return false;
}

bool CAutotextPart::GetExpanded(CStringW& val, bool modifier /*= false*/, bool default_val /*= false*/) const
{
	val.Empty();
	return AppendExpanded(val, modifier, default_val);
}

bool CAutotextPart::GetExpandedEx(CStringW& val, bool $_to_$$, bool va_kw, bool env_var, bool modifier /*= false*/,
                                  bool default_val /*= false*/) const
{
	val.Empty();
	return AppendExpandedEx(val, $_to_$$, va_kw, env_var, modifier, default_val);
}

bool CAutotextPart::GetCollapsed(CStringW& val, bool modifier /*= false*/, bool default_val /*= false*/) const
{
	val.Empty();
	return AppendCollapsed(val, modifier, default_val);
}

size_t GetAutotextParts(const CStringW& snippet, std::vector<CAutotextPartPtr>& parts,
                        bool whole_if_no_match /*= false*/)
{
	LPCWSTR pos = snippet;
	LPCWSTR prefix_start = pos;
	LPCWSTR e_pos = pos + snippet.GetLength();

	std::map<std::wstring, CAutotextPartPtr> keywords;
	std::map<std::wstring, CAutotextPartWeakPtrVecPtr> user_keywords;
	std::map<std::wstring, std::wstring> user_keyword_dft_vals;
	std::map<std::wstring, CAutotextPartWeakPtrVecPtr> kw_groups;

	auto set_user_default_value = [&user_keywords](const std::wstring& name, const std::wstring& val) {
		auto it = user_keywords.find(name);
		if (it != user_keywords.end() && it->second)
		{
			for (auto& kw : *it->second)
			{
				if (!kw.expired())
				{
					CAutotextPartPtr p = kw.lock();
					p->SetDefaultValue(val.c_str());
				}
			}
		}
	};

	// #VASnippetsKWRegex
	const auto& rgx = GetKeywordsRegex();
	std::wcmatch m;

	while (std::regex_search(pos, e_pos, m, rgx))
	{
		if (m[4].first == m[0].first && m[4].second == m[0].second)
		{
			// [case: 95951] mimic behavior of old engine
			pos = m[0].second;
			continue; // skip - this is not a valid keyword
		}

		std::wstring match = m[0];  // whole match
		std::wstring name = m[1];   // name of field
		std::wstring modif = m[2];  // optional casing modifier
		std::wstring dftval = m[3]; // optional default value

		if (prefix_start != m[0].first)
		{
			CStringW prefix(prefix_start, ptr_sub__int(m[0].first, prefix_start));
			parts.push_back(CAutotextPartPtr(new CAutotextPartCode(prefix)));
		}

		bool is_user_keyword = !AutotextManager::IsReservedString(match.c_str());

		if (is_user_keyword && !dftval.empty())
		{
			// update default value in existing keywords
			set_user_default_value(name, dftval);

			// set value for future keywords
			user_keyword_dft_vals[name] = dftval;
		}

		// check if such keyword already exists in set
		// if so, add only reference to already existing
		// part, else create a new part

		auto kw_key = name + modif; // name + modifier
		auto it = keywords.find(kw_key);
		if (it != keywords.end())
		{
			// if this keyword already exists in set,
			// then push only reference to it
			it->second->occurrence++;
			parts.push_back(it->second);
		}
		else
		{
			// create new instance

			if (is_user_keyword && dftval.empty())
			{
				// try to find default value in list
				auto uk_it = user_keyword_dft_vals.find(name);
				if (uk_it != user_keyword_dft_vals.end())
					dftval = uk_it->second;
			}

			CAutotextPartPtr ptr;

			if (!is_user_keyword)
			{
				if (name == L"selected")
					ptr = CAutotextPartPtr(new CAutotextPartSelected());
				else if (match == L"$$")
					ptr = CAutotextPartPtr(new CAutotextPartDollar());
				else
					ptr = CAutotextPartPtr(new CAutotextPartKeyword(name.c_str(), modif.c_str()));
			}
			else
			{
				ptr = CAutotextPartPtr(new CAutotextPartUserKW(name.c_str(), modif.c_str(), dftval.c_str()));
			}

			if (ptr)
			{
				parts.push_back(ptr);
				ptr->occurrence++;

				// insert it also to helpers

				keywords[kw_key] = ptr;

				auto& vec = is_user_keyword ? user_keywords[name] : kw_groups[name];

				if (!vec)
					vec.reset(new IAutotextPartWeakPtrVec());

				vec->push_back(ptr);
			}
		}

		pos = m[0].second;
		prefix_start = pos;
	}

	// append last part

	if (parts.empty() && whole_if_no_match)
		parts.push_back(CAutotextPartPtr(new CAutotextPartCode((LPCWSTR)snippet)));
	else if (m.suffix().length())
		parts.push_back(CAutotextPartPtr(new CAutotextPartCode(m.suffix().str().c_str())));

	// set group for all user keywords
	for (auto& vPair : user_keywords)
	{
		if (vPair.second && vPair.second->size() > 1)
		{
			for (auto& weak_p : *vPair.second)
			{
				if (!weak_p.expired())
				{
					CAutotextPartPtr p = weak_p.lock();
					p->group = vPair.second;
				}
			}
		}
	}

	// set group for all other keywords
	for (auto& vPair : kw_groups)
	{
		if (vPair.second && vPair.second->size() > 1)
		{
			for (auto& weak_p : *vPair.second)
			{
				if (!weak_p.expired())
				{
					CAutotextPartPtr p = weak_p.lock();
					p->group = vPair.second;
				}
			}
		}
	}

	auto st = std::make_shared<SYSTEMTIME>();
	auto g = std::make_shared<GUID>();

	if (AutotextManager::GetGuid(*g) && AutotextManager::GetDateAndTime(*st))
	{
		// set guid and time in each part
		for (auto& part : parts)
		{
			part->guid = g;
			part->time = st;
		}
	}

	// set next and previous
	for (size_t i = 1; i < parts.size(); i++)
	{
		parts[i - 1]->next = parts[i];
		parts[i]->prev = parts[i - 1];
	}

	return parts.size();
}

CStringW EscapeXMLSpecialChars(const CStringW& input)
{
	CStringW str_out;
	for (int i = 0; i < input.GetLength(); i++)
	{
		wchar_t ch = input[i];
		switch (ch)
		{
		case L'"':
			str_out.Append(L"&quot;");
			break;
		case L'\'':
			str_out.Append(L"&apos;");
			break;
		case L'<':
			str_out.Append(L"&lt;");
			break;
		case L'>':
			str_out.Append(L"&gt;");
			break;
		case L'&':
			str_out.Append(L"&amp;");
			break;
		default:
			str_out.AppendChar(ch);
			break;
		}
	}
	return str_out;
}

bool ConvertVaSnippetToVsSnippetXML(EdCntPtr ed, CStringW& out_vs_snipp_XML, const CStringW& in_va_snipp,
                                    IndentingMode indent, KeyWords preserve_kws /*= kw_default*/)
{
	try
	{
		CStringW selection = ed->GetSelStringW();
		CStringW clipboard = ::GetClipboardText(ed->GetSafeHwnd());

		CStringW snippet = in_va_snipp;

		if (indent != IndentingMode::None)
			AutotextManager::ApplyVAFormatting(snippet, selection, clipboard, ed.get());

		std::vector<std::shared_ptr<CAutotextPart>> parts;
		if (GetAutotextParts(snippet, parts))
		{
			if (ConvertAutotextPartsToVsSnippetXML(out_vs_snipp_XML, parts, selection, clipboard, preserve_kws))
			{
				return true;
			}
		}
	}
	catch (const std::exception& e)
	{
		//		LPCSTR what = e.what();
		(void)e;
		_ASSERTE(!"Exception in ::ConvertVaSnippetToVsSnippetXML");
	}
	catch (...)
	{
		_ASSERTE(!"Exception in ::ConvertVaSnippetToVsSnippetXML");
	}

	return false;
}

bool ConvertAutotextPartsToVsSnippetXML(CStringW& out_vs_snipp_XML, std::vector<CAutotextPartPtr>& parts,
                                        LPCWSTR selection, LPCWSTR clipboard, KeyWords preserve_kws /*= kw_default*/)
{
	CStringW decls, code;
	CStringW types = L"<SnippetType>Expansion</SnippetType>";

	bool end_defined = false;
	bool have_surround_with = false;

	std::set<CStringW> decl_list;

	for (auto part : parts)
	{
		// For the first pass, revert to our input prompt
		// if any modifiers are present in the snippet source.
		if (part->IsUserKeyword() && part->HasModifier())
			return false;

		// code parts are appended as are
		if (part->IsCode())
			part->AppendExpandedEx(code, true, false, true);

		// User input keywords need to be reformatted
		else if (part->IsUserKeyword())
		{
			// Append Declaration of this keyword if does not already exist
			CStringW name;
			if (part->GetName(name) && decl_list.find(name) == decl_list.end())
			{
				decl_list.insert(name);
				part->AppendDeclarationTag(decls);
			}

			// Append in format $name$ w/o default value and modifier
			part->AppendCollapsed(code);

			if (part->next.expired())
			{
				// If there are multiple items in the group
				// then this item is not unique, and as it
				// is the last one, it is not editable,
				// because editable is only first in group.

				if (!part->group || part->group->size() == 1)
				{
					// [case: 88913]
					// revert to old input method when the snippet
					// source ends with editable user input item.
					return false;
				}
			}
		}

		// Other keywords are reformatted or pushed as are
		// it depends on their behavior.
		else if (part->IsKeyword())
		{
			// $end$ keyword it handled by VS
			if ((preserve_kws & kw_end) && part->NameIs(L"end"))
			{
				end_defined = true;
				part->AppendCollapsed(code);
			}

			// $$ keyword it handled by VS
			else if ((preserve_kws & kw_dollar) && part->NameIs(L""))
			{
				part->AppendCollapsed(code);
			}

			// Also $selected$ keyword could be handled by VS,
			// but it does not handle any linebreak rules, so
			// it is almost unusable. However for some refactor
			// needs it could be OK, so one can preserve it.
			else if ((preserve_kws & kw_selected) && part->NameIs(L"selected"))
			{
				part->AppendCollapsed(code);

				if (!have_surround_with)
				{
					// add SurroundsWith as SnippetTypes tag
					have_surround_with = true;
					types = L"<SnippetType>SurroundsWith</SnippetType>";
				}
			}

			else
			{
				// try to handle passed selection and clipboard
				// if those are nullptr, let it be...
				if (selection && part->NameIs(L"selected"))
				{
					CStringW sel(selection);
					sel.Replace(L"$", L"$$");
					code.Append(sel);
				}
				else if (clipboard && part->NameIs(L"clipboard"))
				{
					CStringW cb(clipboard);
					cb.Replace(L"$", L"$$");
					code.Append(cb);
				}

				// other keywords are handled by VA
				else if (preserve_kws & kw_other)
					part->AppendCollapsed(code, true, true);
				else
					part->AppendExpandedEx(code, true, false, true, false, true);
			}
		}
	}

	if (!end_defined)
	{
		// VA Snippets end by default at the end of snippet, while VS Snippets
		// end at start. We could define end position by ExpansionClient,
		// but simpler way is to append $end$ keyword into each snippet.

		CAutotextPartKeyword kw(L"end");
		kw.AppendCollapsed(code);
	}

	const LPCWSTR vsSnippFmt = LR"(
		<CodeSnippets  xmlns='http://schemas.microsoft.com/VisualStudio/2005/CodeSnippet'>
			<CodeSnippet Format='1.0.0'>
				<Header>
				  <Title>VA</Title>
				  <Description>VA</Description>
				  <Author>VA</Author>
				  <Shortcut></Shortcut>
				  <SnippetTypes>
					%s
				  </SnippetTypes>          
				</Header>
				<Snippet>
					<Declarations>
						%s
					</Declarations>
					<Code Language='CSharp'>%s</Code>
				</Snippet>
			</CodeSnippet>
		</CodeSnippets>
		)";

	// CDATA is unfortunately needed to
	// preserve white spaces in code text.
	// Simple escaping of special chars caused
	// trimmed text being added to expansion.
	// Method WrapInCDATA allows also nested
	// CDATA section by splitting of end "]]>".
	code = WrapInCDATA(code);

	CString__FormatW(out_vs_snipp_XML, vsSnippFmt, (LPCWSTR)types, (LPCWSTR)decls, (LPCWSTR)code);
	return true;
}

bool InsertVsSnippetXML(EdCntPtr ed, LPCWSTR vsSnippXML, IndentingMode indenting)
{
	try
	{
		if (ed && ed->m_IVsTextView)
		{
			IXMLDOMDocumentPtr dom;

			// create instance of DOM document
			if (SUCCEEDED(dom.CreateInstance(__uuidof(DOMDocument))))
			{
				// Load the snippet XML into DOM and get Snippet node

				VARIANT_BOOL xml_load_status = VARIANT_FALSE;
				dom->put_async(VARIANT_FALSE);
				dom->put_validateOnParse(VARIANT_TRUE);
				dom->put_resolveExternals(VARIANT_TRUE);
				if (SUCCEEDED(dom->loadXML(_bstr_t(vsSnippXML), &xml_load_status)) && xml_load_status == VARIANT_TRUE)
				{
					// interface is handled by C# as it has best engine
					GUID csharp_lang_guid;
					::UuidFromStringA((RPC_CSTR) "694DD9B6-B865-4C5B-AD85-86356E9C88DC", &csharp_lang_guid);
					if (AutotextExpansionClient::InsertExpansion(ed, dom, csharp_lang_guid, indenting))
					{
						if (wcsstr(vsSnippXML, L"#include") || wcsstr(vsSnippXML, L"#import"))
							ed->SetTimer(DSM_VA_LISTMEMBERS, 50, NULL);

						return true;
					}
				}
			}
		}
		else
		{
			_ASSERTE(FALSE);
		}
	}
	catch (const _com_error& e)
	{
		_bstr_t bstrMessage(e.ErrorMessage());
		_bstr_t bstrSource(e.Source());
		_bstr_t bstrDescription(e.Description());
		_ASSERTE(!"COM exception");
	}
	catch (const std::exception& e)
	{
		//		LPCSTR what = e.what();
		(void)e;
		_ASSERTE(!"STL exception");
	}
	catch (CException* e)
	{
		TCHAR buff[512];
		e->GetErrorMessage(buff, 512);

		_ASSERTE(!"MFC/ATL exception");

		e->Delete();
	}
	catch (...)
	{
		_ASSERTE(!"Unknown exception");
	}

	return false;
}

CStringW CreateIndentString(long num_columns)
{
	CStringW out_str;

	// tabs
	ulong num_tabs = num_columns / Psettings->TabSize;
	for (ulong i = 0; i < num_tabs; i++)
		out_str.AppendChar(L'\t');

	// spaces
	ulong num_spcs = num_columns - num_tabs * Psettings->TabSize;
	for (ulong i = 0; i < num_spcs; i++)
		out_str.AppendChar(L' ');

	return out_str;
}

CStringW GetIndentString(const CStringW& line)
{
	CStringW out_str;
	for (int i = 0; i < line.GetLength(); i++)
	{
		WCHAR ch = line[i];
		if (ch == '\t' || ch == ' ')
			out_str += ch;
		else
			break;
	}
	return out_str;
}

long FindIndentPosition(const CStringW& line)
{
	long length = 0;
	for (int i = 0; i < line.GetLength(); i++)
	{
		WCHAR ch = line[i];
		if (ch == '\t')
			length = long((length / Psettings->TabSize + 1) * Psettings->TabSize);
		else if (ch == ' ')
			length++;
		else
			break;
	}
	return length;
}

long FindIndentPositionForIndex(const CStringW& line, long index)
{
	long length = 0;

	if (index > line.GetLength())
		index = line.GetLength();

	for (int i = 0; i < index; i++)
	{
		WCHAR ch = line[i];
		if (ch == '\t')
			length = long((length / Psettings->TabSize + 1) * Psettings->TabSize);
		else
			length++;
	}
	return length;
}

HRESULT FindXMLNode(IXMLDOMNode* in_root, const _bstr_t& in_node_name, CComPtr<IXMLDOMNode>& out_node)
{
	CComPtr<IXMLDOMNode> node, node_tmp;
	if (in_root && SUCCEEDED(in_root->get_firstChild(&node)))
	{
		do
		{
			_bstr_t name;
			if (node && SUCCEEDED(node->get_nodeName(name.GetAddress())))
			{
				if (name == in_node_name)
				{
					out_node = node;
					return S_OK;
				}
				else if (SUCCEEDED(FindXMLNode(node, in_node_name, out_node)))
					return S_OK;
			}
			node_tmp.Attach(node.Detach());
		} while (node_tmp && SUCCEEDED(node_tmp->get_nextSibling(&node)));
	}
	return E_FAIL;
}

bool GetXMLNodeText(IXMLDOMNode* in_root, const _bstr_t& in_node_name, _bstr_t& out_text)
{
	CComPtr<IXMLDOMNode> node;
	return SUCCEEDED(FindXMLNode(in_root, in_node_name, node)) && node &&
	       SUCCEEDED(node->get_text(out_text.GetAddress()));
}

bool GetXMLNodeTextLines(IXMLDOMNode* in_root, const _bstr_t& in_node_name, std::vector<CStringW>& lines)
{
	_bstr_t node_text;
	if (GetXMLNodeText(in_root, in_node_name, node_text))
	{
		lines.push_back(CStringW());
		CStringW* lineP = &lines.back();

		for (LPCWSTR wstr = node_text; wstr && *wstr; wstr++)
		{
			WCHAR ch = *wstr;
			if (ch == '\n' || (ch == '\r' && wstr[1] != '\n'))
			{
				lineP->AppendChar(ch);
				lines.push_back(CStringW());
				lineP = &lines.back();
			}
			else
			{
				lineP->AppendChar(ch);
			}
		}

		return true;
	}

	return false;
}

CStringW WrapInCDATA(const CStringW& input)
{
	// This is little tricky, but I found that nesting CDATA is possible
	// by splitting all occurrences of "]]>" into multiple CDATA sections.
	// First CDATA will contain "]]" and another ">", that's it.
	//
	// From the link: http://en.wikipedia.org/wiki/CDATA#Nesting
	// A CDATA section cannot contain the string "]]>" and therefore it is not possible
	// for a CDATA section to contain nested CDATA sections.The preferred approach to using
	// CDATA sections for encoding text that contains the triad "]]>" is to use multiple CDATA
	// sections by splitting each occurrence of the triad just before the ">".
	// For example, to encode "]]>" one would write:
	//
	// <![CDATA[]]]]><![CDATA[>]]>
	//
	// This means that to encode "]]>" in the middle of a CDATA section,
	// replace all occurrences of "]]>" with the following:
	//
	// ]]]]><![CDATA[>
	//
	// This effectively stops and restarts the CDATA section.

	CStringW data = input;

	// first we need to replace all occurrences of "]]>"
	data.Replace(L"]]>", L"]]]]><![CDATA[>");

	// now we can wrap it up...
	data.Insert(0, L"<![CDATA[");
	data.Append(L"]]>");

	return data;
}

// Poor replacement of environment variables in order they are defined,
// which simulates default behavior of ExpandEnvironmentStrings API.
// So nested variables are expanded only in case when they are defined
// after the variable that includes them.
void ExpandEnvironmentStringsEx(CStringW& in_out_str, LPCWSTR dollarSubstitution)
{
	if (in_out_str.Find(L'%') != -1)
	{
		std::wstring wstr = (LPCWSTR)in_out_str;

		ForEachEnvVariable([&](LPCWSTR n, int nLen, LPCWSTR v, int vLen) {
			CStringW val(v, vLen);
			CStringW name(n, nLen);

			if (gTestsActive && name.CompareNoCase(L"USERNAME") == 0)
				val = L"AST_RUNNER";

			if (dollarSubstitution)
				val.Replace(L"$", dollarSubstitution);

			name.Insert(0, L"[%]");
			name.Append(L"[%]");

			std::wregex rgx((LPCWSTR)name, std::wregex::ECMAScript | std::wregex::icase);
			wstr = std::regex_replace(wstr, rgx, (LPCWSTR)val);

			return true;
		});

		in_out_str = wstr.c_str();
	}
}

void ExpandVASpecificKeywords(CStringW& snipp, KeyWords preserve_kw, LPCWSTR dollarSubstitution, const GUID* guid,
                              const SYSTEMTIME* time)
{
	EdCntPtr ed(g_currentEdCnt);

	auto should_preserve = [&](const ReservedString* rs) -> bool {
		if ((preserve_kw & kw_selected) && !strcmp("selected", rs->KeyWord))
			return true;

		if ((preserve_kw & kw_end) && !strcmp("end", rs->KeyWord))
			return true;

		if ((preserve_kw & kw_dollar) && !strcmp("", rs->KeyWord))
			return true;

		return 0 != (preserve_kw & kw_other);
	};

	///////////////////////////////
	// expand VA reserved strings

	AutotextManager::ForEachReservedString([&](const ReservedString* rs, char modif, const WTString& str) {
		if (!should_preserve(rs))
		{
			CStringW strw = str.Wide();
			if (snipp.Find(strw) >= 0)
			{
				WTString kw = str;
				AutotextManager::ExpandReservedString(ed, kw, guid, time);
				if (kw != str)
				{
					CStringW kwWide = kw.Wide();

					if (dollarSubstitution)
						kwWide.Replace(L"$", dollarSubstitution);

					snipp.Replace(strw, kw.Wide());
				}
			}
		}

		return true;
	});
}

// iterates through all environment variables
// ******************************************
// Lambda:
// return: true to continue in iteration
// arg0 = name (not zero terminated)
// arg1 = name length
// arg2 = value
// arg3 = value length
void ForEachEnvVariable(std::function<bool(LPCWSTR, int, LPCWSTR, int)> process_var)
{
	if (!process_var)
		return;

	// RAII way to handle buffer
	struct EnvVars
	{
		LPWCH s;
		EnvVars()
		{
			s = GetEnvironmentStringsW();
		}
		~EnvVars()
		{
			FreeEnvironmentStringsW(s);
		}
	} _envVars;

	LPWCH envWstr = _envVars.s;

	// Iterate through list in form:
	//
	// Var1=Value1\0
	// Var2=Value2\0
	// Var3=Value3\0
	// ...
	// VarN=ValueN\0\0

	for (; *envWstr; envWstr++)
	{
		LPCWSTR name = nullptr, val = nullptr;
		int nameLen = 0, valLen = 0;

		while (*envWstr)
		{
			if (val != nullptr)
				valLen++;
			else if (*envWstr == '=') // first '=' changes the destination
			{
				// assign val, so that first 'if' condition has precedence
				val = ++envWstr;
				valLen = 1;
				continue;
			}
			else
			{
				// val is NULL and envWstr does not point to '=',
				// so we are filling the name of variable.

				if (name == nullptr)
					name = envWstr;

				nameLen++;
			}

			envWstr++;
		}

		// we should have name and value assigned
		if (name && val && nameLen && name != val)
			if (!process_var(name, nameLen, val, valLen))
				break;
	}
}

// #VASnippetsKWRegex
const std::wregex& GetKeywordsRegex()
{
	// not assigned until really needed

	static const std::wregex rgx(
	     L"[$][$]" // $$ combo
	     L"|"
	     L"[$]"                                                                    // start of KW
	     L"([\\w_]+?)"                                                             // [required] name
	     L"(_?(?:[Pp]ascal|PASCAL|[Cc]amel|CAMEL|[Uu]pper|UPPER|[Ll]ower|LOWER))?" // [optional] casing modifier
	     L"(?:=((?:[$][\\w_]+?[$]|[^\\r\\n$])+))?" // [optional] default value (may include keywords except $$)
	     L"[$]"                                    // end of KW
	     L"|"
	     L"([$][^\\r\\n$]+?[$])" // [case: 95951] mimic behavior of HideVaSnippetEscapes
	     ,
	     std::regex::ECMAScript | std::regex::optimize);

	return rgx;
}
