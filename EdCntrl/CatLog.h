#pragma once

#include <bitset>
#include <tuple>

using LogCategoryId = int;
using LogCategoryName = const char*;
using LogCategory = std::tuple<LogCategoryId, LogCategoryName>;

constexpr LogCategory log_categories[]
{
	// internal categories
    { 0, "Reserved" },      // we reserve 0 to avoid calling bitset::test with -1 when there is an error while using the system directly,
                            // without the __MyCatLog macro which has static_assert. Also, we use 0 when the category is not found.
    { 1, "Uncategorized" }, // This must be the second entry with this name because we use 1 as the default category at same places
	{ 2, "LogSystem.TimeTracking" }, // Used internally in the logging system
    { 3, "LogSystem.LogFunc" }, // Used internally in the logging system

	{ 4, "LogSystem.Trace" },
	{ 5, "Environment.Directories" },
    { 6, "Environment.DumpToLog" },
    { 7, "Environment.Project" },
    { 8, "Environment.Solution" },
    { 9, "Settings" },
    { 10, "Settings.CheckExtStringFormat" },
    { 11, "Licensing.LogStr" },
    { 12, "Licensing.GetLicenseStatus" },
    { 13, "Editor" },
    { 14, "Editor.Events" }, // editor (EdCnt) or editor related events such as VACompletionSet, etc.
    { 15, "Editor.Timer" },
    { 16, "Parser.ParserThread" },
    { 17, "Parser.PooledThread" },
    { 18, "Parser.FileParserWorkItem" },
	{ 19, "Parser.MultiParse" },
    { 20, "Parser.BaseClassFinder" },
    { 21, "Parser.VAHashTable" },
    { 22, "Parser.FileName" }, // places where the parser outputs a filename for some reason
    { 23, "Parser.FindReferences" },
    { 24, "Parser.VAParse" },
    { 25, "Parser.Scope" },
    { 26, "Parser.Import" },
    { 27, "LowLevel" }, // low level stuff
    { 28, "Parser.FileFinder" },
};

constexpr size_t LogCategoriesCount = sizeof(log_categories) / sizeof(log_categories[0]);
extern std::bitset<LogCategoriesCount> gLoggingCategories;

void SetBitsForMatchingCategories(const std::string& input);
void ClearBitsForMatchingCategories(const std::string& input);
void LogCategories(const char* message = nullptr);
void InitCategories();

consteval bool compareStrings(const char* a, const char* b)
{
	for (; *a && *b; ++a, ++b)
	{
		if (*a != *b)
		{
			return false;
		}
	}
	return *a == *b;
}

consteval int GetIdByString(const char* str)
{
	for (const auto& category : log_categories)
	{
		if (compareStrings(str, std::get<LogCategoryName>(category)))
		{
			return std::get<LogCategoryId>(category);
		}
	}

	// unknown category: it will cause a compiler error due to the way the macro is written.
	// you may even get an intellisense underline, but intellisense isn't reliable in all files
	return 0;
}
