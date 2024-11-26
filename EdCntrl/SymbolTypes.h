#pragma once

// Symbol types
// (stored in DType::mTypeAndDbFlags)
// do not change order since that will break testsuite goldfiles
enum
{
	UNDEF,
	CLASS,
	STRUCT,
	FUNC,
	VAR,
	DEFINE,
	TYPE,
	CachedBaseclassList,
	VaMacroDefArg, // scope of :1
	RESWORD,
	SYM,
	NUMBER,
	OPERATOR,
	ASM,
	vaInclude, // include directive
	STRING,
	COMMENT,
	vaIncludeBy, // include directive reverse lookup
	CTEXT,
	PREPROCSTRING,
	Lambda_Type,
	FOLDER_TYPE,
	LINQ_VAR,
	GOTODEF, // typically only applicable to C++ implementations that are independent of the declaration
	FILE_TYPE,
	LVAR, // used during spell check
	CONSTANT,
	DELEGATE,
	C_ENUM,
	C_ENUMITEM,
	EVENT,
	C_INTERFACE,
	MAP,     // value never assigned - maybe created for VS icons?
	MAPITEM, // value never assigned - maybe created for VS icons?
	MODULE,
	NAMESPACE,
	PROPERTY,
	TEMPLATE, // value never assigned - maybe created for VS icons?
	TEMPLATETYPE,
	TAG, // <b>, </b>
	// UI controls
	Control_First,
	Control_Button,
	Control_Radio,
	Control_Label,
	Control_ListBox,
	Control_ComboBox,
	Control_Text,
	Control_Checkbox,
	Control_Bool,
	Control_Result,
	Control_Timer,
	Control_DateTimePicker,
	Control_GridView,
	Control_Other,
	Control_Last,

	vaInheritsFrom,
	vaHashtag,
	vaInheritedBy,
	TEMPLATE_DEDUCTION_GUIDE, // user-defined deduction guides look like function declarations
	                          // http://en.cppreference.com/w/cpp/language/class_template_argument_deduction
	VaMacroDefNoArg,          // scope of :2

	NOTFOUND = 0x003F
};

#define TYPEMASK 0x003F

// VaDBFile flags (VA_DB*)
// or'd with Symbol type
// (stored in DType::mTypeAndDbFlags)
// do not change order since that will break testsuite goldfiles
// add new values before first
// see VADatabase::DBIdFromDbFlags for mapping to DbTypeId
#define VA_DB_FLAGS 0xFE000000
#define VA_DB_FLAGS_MASK 0x01FFFFFF
#define VA_DB_SolutionPrivateSystem                                                                                    \
	0x02000000 // semi-attribute: not applicable without either VA_DB_Cpp or VA_DB_Net; combination corresponds to
	           // DbTypeId_SolutionSystemCpp or DbTypeId_SolutionSystemNet
#define VA_DB_BackedByDatafile                                                                                         \
	0x04000000 // attribute: does not correspond to a DbTypeId; is a flag that DType item strs can be read from file via
	           // DType::DoGetStrs()
#define VA_DB_Solution 0x08000000       // DbTypeId_Solution
#define VA_DB_ExternalOther 0x10000000  // DbTypeId_ExternalOther (non-sys/non-sln DB)
#define VA_DB_LocalsParseAll 0x20000000 // DbTypeId_LocalsParseAll	locals (stored in solution locals dir) DB
#define VA_DB_Cpp 0x40000000            // DbTypeId_Cpp		 C++ (system includes/mfc) DB
#define VA_DB_Net 0x80000000            // DbTypeId_Net		 Managed / .NET DB

// additional types used in completion lists
// if adding here, see also PoptypeHasPriorityOverVsCompletion
#define ET_MASK 0xffff0000
#define ET_MASK_RESULT 0x55550000
#define ET_EXPAND_TAB 0x55550001
#define ET_EXPAND_MEMBERS 0x55550004
#define ET_EXPAND_COMLETE_WORD 0x55550005
#define ET_EXPAND_VSNET 0x55550006
#define ET_SUGGEST 0x55550007
#define ET_AUTOTEXT 0x55550008
#define ET_VS_SNIPPET 0x55550009 // vs snippets that we put in list ourselves
#define ET_EXPAND_TEXT 0x55550011
#define ET_SUGGEST_BITS 0x55550012
#define ET_EXPAND_INCLUDE 0x55550013 // Special flag to only expand includes/imports.
#define ET_SCOPE_SUGGESTION 0x55550014
#define ET_AUTOTEXT_TYPE_SUGGESTION 0x55550015

bool ExptypeHasPriorityOverVsCompletion(int expType);

inline BOOL IS_OBJECT_TYPE(unsigned int type)
{
	if ((type & ET_MASK) == ET_MASK_RESULT)
		return FALSE;

	_ASSERTE((type & TYPEMASK) == type);
	switch (type)
	{
	case CLASS:
	case STRUCT:
	case TYPE:
	case C_ENUM:
	case C_INTERFACE:
	case NAMESPACE:
	case TEMPLATETYPE:
	case MODULE:
	case TAG:
		return TRUE;
	}
	return FALSE;
}

// Symbol attributes (V_*)
// (stored in DType::mAttributes)
// do not change order since that will break testsuite goldfiles
#define V_HIDEFROMUSER 0x00000001   // directive to prevent display of item to user
#define V_ABSTRACT_CLASS 0x00000002 // shared: abstract class value is same as pure method value
#define V_PURE_METHOD 0x00000002    // shared: pure method value is same as abstract class value
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
#define V_PUBLISHED                                                                                                    \
	0x00000004 // [case: 135860] C++ Builder __published shares same value as "internal" since internal is not valid in
	           // C++ Builder
#endif
#define V_INTERNAL 0x00000004      // internal visibility
#define V_SEALED 0x00000008        // sealed class or method
#define V_NEW_METHOD 0x00000010    // shared: new method value is same as partial class value
#define V_PARTIAL_CLASS 0x00000010 // shared: partial class value is same as new method value
#define V_EXTERN 0x00000020        // shared: extern value is same as virtual value
#define V_VIRTUAL 0x00000020       // shared: virtual value is same as extern value
#define V_INPROJECT 0x00000040
#define V_MANAGED 0x00000080 // special-case managed type (as in enums)
#define V_INFILE 0x00000100
#define V_RESERVEDTYPE                                                                                                 \
	0x00000200 // shared: used in .va files for RESWORDs that are also TYPEs (same value as V_CPPB_CLASSMETHOD)
#if defined(VA_CPPUNIT) || defined(RAD_STUDIO_LANGUAGE) || (defined(SEAN) && defined(_DEBUG))
#define V_CPPB_CLASSMETHOD                                                                                             \
	0x00000200 // shared: for C++ Builder methods declared with __classmethod (same value as V_RESERVEDTYPE)
#endif
#define V_SYSLIB 0x00000400 // found in include path
#define V_POINTER 0x00000800
#define V_LOCAL 0x00001000          // local var
#define V_PROTECTED 0x00002000      // visibility
#define V_PRIVATE 0x00004000        // visibility
#define V_CONSTRUCTOR 0x00008000    // foo::foo();
#define V_IMPLEMENTATION 0x00010000 // method attribute
#define V_FILENAME                                                                                                     \
	0x00020000 // #include completion list filename.  also used for internal housekeeping with V_HIDEFROMUSER
#define V_VA_STDAFX 0x00040000 // Flag to mark macros defined in misc/stdafx.h files, so they don't get overridden
#define V_IDX_FLAG_INCLUDE 0x00080000    // directive passed to ReadIdxFile
#define V_IDX_FLAG_USING 0x00100000      // directive passed to ReadIdxFile
#define V_TEMPLATE_ITEM 0x00200000       // va instantiated templates
#define V_PREFERREDDEFINITION 0x00400000 // template <class T> // T is a template type
#define V_DONT_EXPAND 0x00800000         // directive to prevent macro expansion
#define V_INCLUDED_MEMBER 0x01000000     // items parsed via scoped #include
#define V_DETACH 0x02000000              // marked for future hash tree detach/cleanup
#define V_OVERRIDE 0x04000000            // override method/property
#define V_TEMPLATE 0x08000000            // template method/function
#define V_REF 0x10000000                 // shared: ref class/var value is same as inline method/func value
#define V_INLINE 0x10000000              // shared: inline method/func value is same as ref class/var value
#define V_STATIC 0x20000000              // static method, member, function, variable
#define V_CONST 0x40000000               // const method, member, variable
#define V_DESTRUCTOR 0x80000000          // Spaghetti only: foo::~foo(); or foo::!foo();

/*
New declaration state flags (not yet in use):
    State				Context
    ---------			-----------------------------------
    internal			class, method
    sealed				class, method
    abstract			class
    pure				method
    new					method								http://msdn.microsoft.com/en-us/library/51y09td4.aspx
    partial				class								http://msdn.microsoft.com/en-us/library/wbx7zzdd.aspx
    virtual				method
    extern				function, variable
    ref					class, variable/parameter
    inline				method, function
    static				method, member, function, variable
    const				method, member, variable

The values for these flags are shared since they have no overlap in context:
    abstract (class)				/	pure (method)
    new (method)					/	partial (class)
    extern (function, var)			/	virtual (method)
    ref (class, var)				/	inline (method, function)
    V_RESERVEDTYPE (reserved word)	/	V_CPPB_CLASSMETHOD (method)

The values for these flags could be shared to make room for new flags:
    V_POINTER (var)					/	V_OVERRIDE (method, property)
    V_CONSTRUCTOR (method)			/	V_FILENAME
    V_IMPLEMENTATION				/	?
 */
