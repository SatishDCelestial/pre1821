#include "StdAfxEd.h"
#include "AutotextManager.h"
#include "DefaultAutotext.h"
#include "FileTypes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static const TemplateItem sDefaultAutotextCpp[] = {
    TemplateItem("namespace (VA)",

                 "namespace $end$\n"
                 "{\n"
                 "	$selected$\n"
                 "}\n",

                 "VA Snippet used by Surround With Namespace.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use.",

                 ""),
    TemplateItem("#ifdef (VA)",

                 "#ifdef $condition=_DEBUG$$end$\n"
                 "$selected$\n"
                 "#endif // $condition$\n",

                 "VA Snippet used by Surround With #ifdef.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use.",

                 "#if"),
    TemplateItem("#region (VA)",

                 "#pragma region $end$$regionName$\n"
                 "$selected$\n"
                 "#pragma endregion $regionName$\n",

                 "VA Snippet used by Surround With #region.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use.",

                 "#r"),
    TemplateItem("{...}",

                 "{\n"
                 "	$end$$selected$\n"
                 "}\n",

                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("(...)",

                 "($selected$)",

                 "If you have modified this item, you may delete it to restore the default upon next use."),
#ifdef AVR_STUDIO
    TemplateItem("SuggestionsForType __attribute__",
                 "address\n"
                 "aligned\n"
                 "const\n"
                 "error\n"
                 "interrupt\n"
                 "io\n"
                 "io_low\n"
                 "mode\n"
                 "naked\n"
                 "noreturn\n"
                 "nothrow\n"
                 "OS_main\n"
                 "OS_task\n"
                 "optimize\n"
                 "packed\n"
                 "progmem\n"
                 "pure\n"
                 "section\n"
                 "signal\n"
                 "visibility\n"
                 "warning\n"
                 "weak\n"
                 "weakref\n",
                 "VA Snippet used for GCC __attribute__ suggestions."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#endif // AVR_STUDIO
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
    TemplateItem("SuggestionsForType __property",
                 "default = \n"
                 "implements\n"
                 "index = \n"
                 "nodefault\n"
                 "read = \n"
                 "stored = true\n"
                 "stored = false\n"
                 "write = \n",
                 "VA Snippet used for __property definition suggestions."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#endif
    TemplateItem("SuggestionsForType bool",
                 "true\n"
                 "false\n",
                 "VA Snippet used for suggestions of type bool."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#ifndef AVR_STUDIO
    TemplateItem("SuggestionsForType BOOL",
                 "TRUE\n"
                 "FALSE\n",
                 "VA Snippet used for suggestions of type BOOL."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#endif // AVR_STUDIO
    TemplateItem("SuggestionsForType class",
                 "public:\n"
                 "private:\n"
                 "protected:\n"
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
                 "__published:\n"
#endif
                 "virtual\n"
                 "void\n"
                 "bool\n"
                 "string\n"
                 "static\n"
                 "const\n",
                 "VA Snippet used for suggestions in class definitions."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#ifndef AVR_STUDIO
    TemplateItem("SuggestionsForType HANDLE",
                 "INVALID_HANDLE_VALUE\n"
                 "NULL\n",
                 "VA Snippet used for suggestions of type HANDLE."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType HRESULT",
                 "S_OK\n"
                 "S_FALSE\n"
                 "E_FAIL\n"
                 "E_NOTIMPL\n"
                 "E_OUTOFMEMORY\n"
                 "E_INVALIDARG\n"
                 "E_NOINTERFACE\n"
                 "E_UNEXPECTED\n",
                 "VA Snippet used for suggestions of type HRESULT."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#endif // AVR_STUDIO
    TemplateItem("SuggestionsForType loop",
                 "continue;\n"
                 "break;\n",
                 "VA Snippet used for suggestions in loops."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType switch",
                 "case \n"
                 "default:\n"
                 "break;\n",
                 "VA Snippet used for suggestions in switch statements."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#ifndef AVR_STUDIO
    TemplateItem("SuggestionsForType VARIANT_BOOL",
                 "VARIANT_TRUE\n"
                 "VARIANT_FALSE\n",
                 "VA Snippet used for suggestions of type VARIANT_BOOL."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
#endif // AVR_STUDIO
    TemplateItem("Refactor Create From Usage Class",
                 "$end$class $ClassName$\n"
                 "{\n"
                 "public:\n"
                 "	$ClassName$($ParameterList$) $colon$\n"
                 "		$MemberInitializationList$\n"
                 "	{\n"
                 "	}\n"
                 "\n"
                 "	~$ClassName$()\n"
                 "	{\n"
                 "	}\n"
                 "\n"
                 "protected:\n"
                 "\n"
                 "private:\n"
                 "	$MemberType$			m$MemberName$;\n"
                 "\n"
                 "};\n",
                 "VA Snippet used for refactoring: Create From Usage.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create From Usage Class (C)",
                 "$end$struct $ClassName$\n"
                 "{\n"
                 "	$ClassName$($ParameterList$)\n"
                 "	{\n"
                 "		$InitializeMember$;\n"
                 "	}\n"
                 "\n"
                 "	$MemberType$			m$MemberName$;\n"
                 "};\n",
                 "VA Snippet used for refactoring: Create From Usage.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
#ifndef AVR_STUDIO
    TemplateItem("Refactor Create From Usage Class (Managed)",
                 "$end$public ref class $ClassName$\n"
                 "{\n"
                 "public:\n"
                 "	$ClassName$($ParameterList$) $colon$\n"
                 "		$MemberInitializationList$\n"
                 "	{\n"
                 "	}\n"
                 "\n"
                 "	~$ClassName$()\n"
                 "	{\n"
                 "	}\n"
                 "\n"
                 "protected:\n"
                 "\n"
                 "private:\n"
                 "	$MemberType$			m$MemberName$;\n"
                 "\n"
                 "};\n",
                 "VA Snippet used for refactoring: Create From Usage.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create From Usage Class (Platform)",
                 "$end$namespace $NamespaceName$\n"
                 "{\n"
                 "	public ref class $ClassName$\n"
                 "	{\n"
                 "	public:\n"
                 "		$ClassName$($ParameterList$) $colon$\n"
                 "			$MemberInitializationList$\n"
                 "		{\n"
                 "		}\n"
                 "\n"
                 "		~$ClassName$()\n"
                 "		{\n"
                 "		}\n"
                 "\n"
                 "	protected:\n"
                 "\n"
                 "	private:\n"
                 "		$MemberType$			m$MemberName$;\n"
                 "\n"
                 "	};\n"
                 "}\n",
                 "VA Snippet used for refactoring: Create From Usage.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
#endif // !AVR_STUDIO
    TemplateItem("Refactor Create From Usage Method Body",
                 "throw std::logic_error(\"The method or operation is not implemented.\");",
                 "VA Snippet used for refactoring: Create From Usage and Implement Interface.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create From Usage Method Body (C)",
                 "assert(!\"The method or operation is not implemented.\");",
                 "VA Snippet used for refactoring: Create From Usage and Implement Interface.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
#ifndef AVR_STUDIO
    TemplateItem("Refactor Create From Usage Method Body (Managed)", "throw gcnew System::NotImplementedException();",
                 "VA Snippet used for refactoring: Create From Usage and Implement Interface.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create From Usage Method Body (Platform)",
                 "throw ref new Platform::NotImplementedException();",
                 "VA Snippet used for refactoring: Create From Usage and Implement Interface.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem(
        "Refactor Create From Usage Method Body (Unreal Engine Virtual Method)",
        "Super::$MethodName$($MethodArgs$);\nthrow std::logic_error(\"The method or operation is not implemented.\");",
        "VA Snippet used for refactoring: Create From Usage and Implement Interface.\n"
        "If you have modified this item, you may delete it to restore the default upon next use."),
#endif // !AVR_STUDIO
    TemplateItem("Refactor Create Header File",
                 "#pragma once\n"
                 "\n"
                 "$body$$end$\n"
                 "\n",
                 "VA Snippet used for refactoring: Create File.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Source File",
                 "#include \"$FILE_BASE$.h\"\n"
                 "\n"
                 "$body$$end$\n"
                 "\n",
                 "VA Snippet used for refactoring: Create File.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation",
                 "\n"
                 "$SymbolType$ $SymbolContext$($ParameterList$) $MethodQualifier$\n"
                 "{\n"
                 "	$end$$MethodBody$\n"
                 "}\n"
                 "\n",
                 "VA Snippet used for refactoring: Change Signature, Create Implementation, and Move Implementation to "
                 "Source File.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation (defaulted)",
                 "\n"
                 "$SymbolType$ $SymbolContext$($ParameterList$) = default;\n"
                 "\n",
                 "VA Snippet used for refactoring: Move Implementation to Source File.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Move Class to New File",
                 "$body$$end$\n"
                 "\n",
                 "VA Snippet used for refactoring: Move Class to New File.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
    TemplateItem("Refactor Create Implementation for Property (read/write)",
                 "__property $SymbolType$ $SymbolName$ = { read = F$SymbolName$, write = Set$SymbolName$ };\n"
                 "\n"
                 "private:\n"
                 "$SymbolType$ F$SymbolName$;\n"
                 "\n"
                 "void Set$SymbolName$($SymbolType$ val)\n"
                 "{\n"
                 "	F$SymbolName$ = val;\n"
                 "}\n"
                 "\n",
                 "VA Snippet used for refactoring: Create Implementation for read/write Property.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation for Property (read-only)",
                 "__property $SymbolType$ $SymbolName$ = { read = F$SymbolName$ };\n"
                 "\n"
                 "private:\n"
                 "$SymbolType$ F$SymbolName$;\n"
                 "\n",
                 "VA Snippet used for refactoring: Create Implementation for read-only Property.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation for Property (array)",
                 "__property $SymbolType$ $SymbolName$$PropertyArrayDef$ = { read = Get$SymbolName$, write = "
                 "Set$SymbolName$ };\n"
                 "\n"
                 "private:\n"
                 "$PropertyArraySize$\n"
                 "static const $SymbolType$ $SymbolName$ErrVal = -1;\n"
                 "$SymbolType$ F$SymbolName$$PropertyArrayFieldDef$;\n"
                 "\n"
                 "$SymbolType$ Get$SymbolName$($PropertyArrayParam$) const\n"
                 "{\n"
                 "	if ($PropertyArrayCheckCond$)\n"
                 "		return F$SymbolName$$PropertyArrayIndex$;\n"
                 "	return $SymbolName$ErrVal;\n"
                 "}\n"
                 "\n"
                 "void Set$SymbolName$($PropertyArrayParam$, $SymbolType$ val)\n"
                 "{\n"
                 "	if ($PropertyArrayCheckCond$)\n"
                 "		F$SymbolName$$PropertyArrayIndex$ = val;\n"
                 "}\n"
                 "\n",
                 "VA Snippet used for refactoring: Create Implementation for array Property with one or more indices.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
#endif
    TemplateItem("Refactor Create Implementation for Member",
                 "\n"
                 "$SymbolType$ $SymbolContext$;\n"
                 "\n",
                 "VA Snippet used for Create Implementation refactoring when used on member variables.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation for Member (header file)",
                 "\n"
                 "__declspec(selectany) $SymbolType$ $SymbolContext$;\n"
                 "\n",
                 "VA Snippet used for Create Implementation refactoring when used on member variables and the target "
                 "is a header file.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation Method Body (Unreal Engine _Validate Method)",
                 "return true;\n",
                 "VA Snippet used for Create Implementation to define the body for the _Validate UE server validation method.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
#ifdef AVR_STUDIO
    TemplateItem("Refactor Document Method", "/**\n"
                                             " * \\brief \n"
                                             " * \n"
                                             " * \\param $MethodArgName$\n"
                                             " * \n"
                                             " * \\return $SymbolType$\n"
                                             " */\n"),
#else
    TemplateItem("Refactor Document Method", "//************************************\n"
                                             "// Method:    $SymbolName$\n"
                                             "// FullName:  $SymbolContext$\n"
                                             "// Access:    $SymbolVirtual$$SymbolPrivileges$$SymbolStatic$\n"
                                             "// Returns:   $SymbolType$\n"
                                             "// Qualifier: $MethodQualifier$\n"
                                             "// Parameter: $MethodArg$\n"
                                             "//************************************\n"),
#endif
    TemplateItem("Refactor Encapsulate Field",
                 "	$end$$SymbolType$ $GeneratedPropertyName$() const { return $SymbolName$; }\n"
                 "	void $GeneratedPropertyName$($SymbolType$ val) { $SymbolName$ = val; }\n"),
    TemplateItem("Refactor Extract Method", "\n"
                                            "$end$$SymbolType$ $SymbolContext$($ParameterList$) $MethodQualifier$\n"
                                            "{\n"
                                            "	$MethodBody$\n"
                                            "}\n"
                                            "\n"),
    // sentinel
    TemplateItem()};

static const TemplateItem sDefaultAutotextCs[] = {
    TemplateItem("namespace (VA)",

                 "namespace $end$\n"
                 "{\n"
                 "	$selected$\n"
                 "}\n",

                 "VA Snippet used by Surround With Namespace.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use.",

                 ""),
    TemplateItem("#region (VA)",

                 "#region $end$\n"
                 "$selected$\n"
                 "#endregion\n",

                 "VA Snippet used by Surround With #region.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use.",

                 "#r"),
    TemplateItem("{...}",

                 "{\n"
                 "	$end$$selected$\n"
                 "}\n",

                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("(...)",

                 "($selected$)",

                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType bool",
                 "true\n"
                 "false\n",
                 "VA Snippet used for suggestions of type bool."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType Boolean",
                 "true\n"
                 "false\n",
                 "VA Snippet used for suggestions of type Boolean."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType class",
                 "public\n"
                 "private\n"
                 "protected\n"
                 "virtual\n"
                 "void\n"
                 "bool\n"
                 "string\n"
                 "static\n"
                 "override\n"
                 "internal\n",
                 "VA Snippet used for suggestions in class definitions."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType loop",
                 "continue;\n"
                 "break;\n",
                 "VA Snippet used for suggestions in loops."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType switch",
                 "case\n" // no space, so user can type "c " and get "case "
                 "default:\n"
                 "break;\n",
                 "VA Snippet used for suggestions in switch statements."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create File",
                 "using System;\n"
                 "\n"
                 "$body$$end$\n"
                 "\n",
                 "VA Snippet used for refactoring: Create File.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create From Usage Class",
                 "$end$namespace $NamespaceName$\n"
                 "{\n"
                 "	public class $ClassName$\n"
                 "	{\n"
                 "		public $ClassName$($ParameterList$)\n"
                 "		{\n"
                 "			$InitializeMember$;\n"
                 "		}\n"
                 "\n"
                 "		private $MemberType$ _$MemberName$;\n"
                 "	}\n"
                 "}\n",
                 "VA Snippet used for refactoring: Create From Usage.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create From Usage Method Body", "throw new NotImplementedException();",
                 "VA Snippet used for refactoring: Create From Usage and Implement Interface.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation",
                 "$SymbolPrivileges$ $SymbolType$ $SymbolName$($ParameterList$)\n"
                 "{\n"
                 "	$end$$MethodBody$\n"
                 "}\n",
                 "VA Snippet used for refactoring: Change Signature, Create Implementation, and Move Implementation to "
                 "Source File.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Document Method", "/// <summary>\n"
                                             "/// $end$\n"
                                             "/// </summary>\n"
                                             "/// <param name=\"$MethodArgName$\"></param>\n"
                                             "/// <returns></returns>\n"),
    TemplateItem("Refactor Encapsulate Field", "	public $SymbolType$ $end$$GeneratedPropertyName$\n"
                                               "	{\n"
                                               "		get { return $SymbolName$; }\n"
                                               "		set { $SymbolName$ = value; }\n"
                                               "	}\n"),
    TemplateItem("Refactor Extract Method", "\n"
                                            "$end$$SymbolPrivileges$ $SymbolType$ $SymbolContext$($ParameterList$)\n"
                                            "{\n"
                                            "	$MethodBody$\n"
                                            "}\n"),
    // sentinel
    TemplateItem()};

static const TemplateItem sDefaultAutotextVB[] = {
    TemplateItem("#region (VA)",

                 "#Region \"$end$\"\n"
                 "$selected$\n"
                 "#End Region\n",

                 "VA Snippet used by Surround With #region.\n"
                 "If you have modified this item, you may delete it to restore the default upon next use.",

                 "#r"),
    TemplateItem("(...)",

                 "($selected$)",

                 "If you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType class",
                 "Public\n"
                 "Private\n"
                 "Protected\n",
                 "VA Snippet used for suggestions in class definitions."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("SuggestionsForType Boolean",
                 "True\n"
                 "False\n",
                 "VA Snippet used for suggestions of type Boolean."
                 "\nIf you have modified this item, you may delete it to restore the default upon next use."),
    TemplateItem("Refactor Create Implementation", "$SymbolPrivileges$ Sub $SymbolName$($ParameterList$)\n"
                                                   "	$end$$MethodBody$\n"
                                                   "End Sub\n"),
    TemplateItem("Refactor Document Method", " \n"
                                             "'//////////////////////////////////////////////////\n"
                                             "' Method:    $SymbolName$\n"
                                             "' FullName:  $SymbolContext$\n"
                                             "' Access:    $SymbolVirtual$$SymbolPrivileges$$SymbolStatic$\n"
                                             "' Returns:   $SymbolType$\n"
                                             "' Parameter: $MethodArg$\n"
                                             "'//////////////////////////////////////////////////\n"),
    TemplateItem("Refactor Encapsulate Field",
                 "	$end$Public Property $GeneratedPropertyName$Property() As $SymbolType$\n"
                 "		Get\n"
                 "			Return $SymbolName$\n"
                 "		End Get\n"
                 "		Set (ByVal value As $SymbolType$)\n"
                 "			$SymbolName$ = value\n"
                 "		End Set\n"
                 "	End Property\n"),
    TemplateItem("Refactor Extract Method", "\n"
                                            "$end$$SymbolPrivileges$ Sub $SymbolName$($ParameterList$)\n"
                                            "	$MethodBody$\n"
                                            "End Sub\n"),
    // sentinel
    TemplateItem()};

bool IsDefaultAutotextItemTitle(int langType, const WTString& title)
{
	if (!title.contains("Refactor ") && !title.contains("SuggestionsForType "))
		return false;

	const TemplateItem* tpl = GetDefaultAutotextItem(langType, title.c_str());
	return tpl ? true : false;
}

const TemplateItem* GetDefaultAutotextItem(int langType, LPCTSTR title)
{
	const TemplateItem* tpl = NULL;
	switch (langType)
	{
	case Src:
	case Header:
	case UC:
		tpl = sDefaultAutotextCpp;
		break;
	case CS:
		tpl = sDefaultAutotextCs;
		break;
	case VBS: // Same defaults as VB case=21969
	case VB:
		tpl = sDefaultAutotextVB;
		break;
	default:
		return NULL;
	}

	for (int idx = 0; !tpl[idx].mTitle.IsEmpty(); ++idx)
	{
		if (tpl[idx].mTitle == title)
			return &tpl[idx];
	}

	return NULL;
}

const TemplateItem* GetDefaultAutotextItem(int langType, int idxIn)
{
	const TemplateItem* tpl = NULL;
	switch (langType)
	{
	case Src:
	case Header:
	case UC:
		tpl = sDefaultAutotextCpp;
		break;
	case CS:
		tpl = sDefaultAutotextCs;
		break;
	case VBS: // Same defaults as VB case=21969
	case VB:
		tpl = sDefaultAutotextVB;
		break;
	default:
		return NULL;
	}

	for (int idx = 0; !tpl[idx].mTitle.IsEmpty(); ++idx)
	{
		if (idx == idxIn)
			return &tpl[idx];
	}

	return NULL;
}
