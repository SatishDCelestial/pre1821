#include "stdafxed.h"
#include "AutotextManager.h"

static const char* Types[] = {"&Date",    "&Time",           "&File", "&Project/Solution",
                              "&General", "&Symbol Context", "G&UID", "&Refactor"};

static const char* Modifs[] = {
    "DULCP", // all modifiers: [none], _UPPER, _LOWER, _CAMEL and _PASCAL
    "D",     // w/o modifiers
    "DU",    // modifiers: [none] and _UPPER
    "DP",    // modifiers: [none] and _PASCAL
    "DL"     // modifiers: [none] and _LOWER
};

#define DATE_TYPE Types[0] // "&Date",
#define TIME_TYPE Types[1] // "&Time",
#define FILE_TYPE Types[2] // "&File",
#define PRSL_TYPE Types[3] // "&Project/Solution",
#define GNRL_TYPE Types[4] // "&General",
#define SYMC_TYPE Types[5] // "&Symbol Context",
#define GUID_TYPE Types[6] // "G&UID",
#define RFTR_TYPE Types[7] // "&Refactor"

#define MODIF_ALL Modifs[0] // "DULCP",
#define MODIF_D Modifs[1]   // "D",
#define MODIF_DU Modifs[2]  // "DU",
#define MODIF_DP Modifs[3]  // "DP",
#define MODIF_DL Modifs[4]  // "DL"

#define UPPER_CASE 'U' // case modifiers are: _UPPER, _LOWER, _CAMEL, _PASCAL
#define lower_case 'L' // case modifiers are: _upper, _lower, _camel, _pascal
#define PascalCase 'P' // case modifiers are: _Upper, _Lower, _Camel, _Pascal

ReservedString AutotextManager::gReservedStrings[] = {
    // { Type, SubType, ModifiersCase, Modifiers, KeyWord, Description }

    ///////////////////////////////////
    // Date

    {DATE_TYPE, NULL, UPPER_CASE, MODIF_D, "DATE", "Year/month/day formatted as %04d/%02d/%02d"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "DATE_&LOCALE", "Current date in locale format"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_D, "DAY", "Day of month formatted as %d"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_D, "DAY_02", "Day of month formatted as %02d"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "DAY&NAME", "Day abbreviation in locale format"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "DAY&NAME_EN", "Day abbreviation in English"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "DAYL&ONGNAME", "Full name of day in locale format"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "DAYL&ONGNAME_EN", "Full name of day in English"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_D, "MONTH", "Month formatted as %d"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_D, "MONTH_02", "Month formatted as %02d"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "&MONTHNAME", "Month abbreviation in locale format"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "&MONTHNAME_EN", "Month abbreviation in English"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "MON&THLONGNAME", "Full name of month in locale format"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "MON&THLONGNAME_EN", "Full name of month in English"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_D, "YEAR", "Year formatted as %d"},
    {DATE_TYPE, NULL, UPPER_CASE, MODIF_D, "YEAR_02", "Year formatted as %02d"},

    ///////////////////////////////////
    // Time

    {TIME_TYPE, NULL, UPPER_CASE, MODIF_D, "HOUR", "Hour formatted as %d"},
    {TIME_TYPE, NULL, UPPER_CASE, MODIF_D, "HOUR_02", "Hour formatted as %02d"},
    {TIME_TYPE, NULL, UPPER_CASE, MODIF_D, "MINUTE", "Minute formatted as %02d"},
    {TIME_TYPE, NULL, UPPER_CASE, MODIF_D, "SECOND", "Second formatted as %02d"},

    ///////////////////////////////////
    // File

    {FILE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "&FILE", "Full filename with path"},
    {FILE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "FILE_&BASE", "Filename without path or extension"},
    {FILE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "FILE_&EXT", "Filename extension"},
    {FILE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "FILE_&PATH", "Path of file"},
    {FILE_TYPE, NULL, UPPER_CASE, MODIF_ALL, "FILE_F&OLDER_NAME", "File folder name without path"},

    ///////////////////////////////////
    // Project/Solution

    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "PROJECT_&FILE", "Full filename of project"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "PROJECT_&NAME", "Filename of project without path or extension"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "PROJECT_&EXT", "Filename extension of project"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "PROJECT_&PATH", "File path of project"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "PROJECT_F&OLDER_NAME", "Project folder name without path"},

    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "SOLUTION_F&ILE", "Full filename of solution"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "SOLUTION_N&AME", "Filename of solution without path or extension"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "SOLUTION_E&XT", "Filename extension of solution"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "SOLUTION_PA&TH", "File path of solution"},
    {PRSL_TYPE, NULL, UPPER_CASE, MODIF_ALL, "SOLUTION_FOLDE&R_NAME", "Solution folder name without path"},

    ///////////////////////////////////
    // General

    {GNRL_TYPE, NULL, lower_case, MODIF_D, "clipboard", "Current clipboard"},
    {GNRL_TYPE, NULL, lower_case, MODIF_D, "end", "Position of caret after expansion"},
    {GNRL_TYPE, NULL, lower_case, MODIF_D, "selected", "Current selection"},
    {GNRL_TYPE, NULL, lower_case, MODIF_D, "", "Literal '$' character"},

    ///////////////////////////////////
    // Symbol Context

    {SYMC_TYPE, NULL, PascalCase, MODIF_D, "MethodName", "Name of containing method"},
    {SYMC_TYPE, NULL, PascalCase, MODIF_D, "MethodArgs", "Method parameters"},
    {SYMC_TYPE, NULL, PascalCase, MODIF_D, "ClassName", "Name of containing class"},
    {SYMC_TYPE, NULL, PascalCase, MODIF_D, "BaseClassName", "Name of base class of containing class"},
    {SYMC_TYPE, NULL, PascalCase, MODIF_D, "NamespaceName", "Fully qualified namespace name"},

    ///////////////////////////////////
    // GUID

    // Note: keywords are w/o '&' within text, that means: Add all items into current popup.
    // in other words, if ampersand is not within keyword, it tells to menu builder not to add new popup menu for it.
    {GUID_TYPE, NULL, UPPER_CASE, MODIF_DU, "GUID_DEFINITION", "Generated GUID formatted for use in a definition"},
    {GUID_TYPE, NULL, UPPER_CASE, MODIF_DU, "GUID_STRING", "Generated GUID formatted for use in a string"},
    {GUID_TYPE, NULL, UPPER_CASE, MODIF_DU, "GUID_STRUCT", "Generated GUID formatted for use in a struct"},
    {GUID_TYPE, NULL, UPPER_CASE, MODIF_DU, "GUID_SYMBOL", "Generated GUID formatted with underscores"},

    ///////////////////////////////////
    // Refactor w/ description

    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "GeneratedPropertyName", "Property name generated during Encapsulate Field"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "generatedPropertyName",
     "Same as $GeneratedPropertyName$ but with lower-case first letter"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "MethodArg", "One parameter of the method and its type"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "MethodArgName", "One parameter of the method"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "MethodArgType", "Type of one parameter of the method"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "MethodBody", "Body of implementation"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "MethodQualifier", "Optional qualifiers of method"},
    {RFTR_TYPE, "FU", PascalCase, MODIF_D, "ParameterList", "Parameters separated by commas"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "SymbolContext", "Context and name of method"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "SymbolName", "Name of method"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "SymbolPrivileges", "Access of method"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "SymbolStatic", "Keyword static or blank"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "SymbolType", "Return type of method"},
    {RFTR_TYPE, NULL, PascalCase, MODIF_D, "SymbolVirtual", "Keyword virtual or blank"},

    ///////////////////////////////////
    // Refactor w/o description

    {RFTR_TYPE, "FU", PascalCase, MODIF_D, "MemberInitializationList", ""},
    {RFTR_TYPE, "FU", PascalCase, MODIF_D, "InitializeMember", ""},
    {RFTR_TYPE, "FU", PascalCase, MODIF_D, "MemberType", ""},
    {RFTR_TYPE, "FU", PascalCase, MODIF_D, "MemberName", ""},
    {RFTR_TYPE, "FU", lower_case, MODIF_D, "colon", ""},
    {RFTR_TYPE, NULL, lower_case, MODIF_D, "body", ""},

    ///////////////////////////////////
    // Terminator

    {0}};
