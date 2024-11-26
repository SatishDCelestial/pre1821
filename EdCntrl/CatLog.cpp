#include "StdAfxEd.h"
#include "CatLog.h"
#include <iostream>
#include <string>
#include "LOG.H"
#include "RegKeys.h"
#include "Registry.h"
#include "SemiColonDelimitedString.h"

std::bitset<LogCategoriesCount> gLoggingCategories;

void DisableLoggingCategoriesFromRegistry()
{
	// Retrieve the registry value as a CString
	CString s = GetRegValue(HKEY_CURRENT_USER, ID_RK_WT_KEY, "DisabledLoggingCategories");

	// Convert the CString to a CStringW (wide string), required by SemiColonDelimitedString
	CStringW sW(s);

	// Create an instance of SemiColonDelimitedString with the wide string
	SemiColonDelimitedString semiColonDelimitedString(sW);

	// Iterate over the items in the delimited string
	CStringW item;
	while (semiColonDelimitedString.NextItem(item))
	{
		item.Trim();
		const char* pNarrow = CW2A(item); // When the CW2A object goes out of scope, it automatically cleans up any memory it allocated for the conversion
		ClearBitsForMatchingCategories(pNarrow);
	}
}

consteval const char* GetStringById(int id)
{
	for (const auto& category : log_categories)
	{
		if (id == std::get<LogCategoryId>(category))
		{
			return std::get<LogCategoryName>(category);
		}
	}
	return nullptr; // Indicate unknown category
}

void catlog(int id, const char* message)
{
	if (gLoggingCategories.test((size_t)id))
	{
		std::cout << id << ": " << message << '\n';
	}
}

void SetBitsForMatchingCategories(const std::string& input)
{
	for (size_t i = 0; i < gLoggingCategories.size(); ++i)
	{
		const char* categoryStr = std::get<LogCategoryName>(log_categories[i]);
		size_t inputLen = input.length();

		if (strncmp(categoryStr, input.c_str(), inputLen) == 0) // Check if categoryStr begins with input
		{
			gLoggingCategories.set(i); // Set bit at index i
		}
	}
}

void ClearBitsForMatchingCategories(const std::string& input)
{
	for (size_t i = 0; i < gLoggingCategories.size(); ++i)
	{
		const char* categoryStr = std::get<LogCategoryName>(log_categories[i]);
		size_t inputLen = input.length();

		if (strncmp(categoryStr, input.c_str(), inputLen) == 0) // Check if categoryStr begins with input
		{
			gLoggingCategories.reset(i); // Clear bit at index i
		}
	}
}

void LogCategories(const char* message)
{
	if (message != nullptr)
		MyLog("%s:", message);

	for (size_t i = 0; i < gLoggingCategories.size(); ++i)
	{
		auto category = log_categories[i];
		const char* categoryName = std::get<LogCategoryName>(category);

        bool isCategorySet = gLoggingCategories.test(i);
		MyLog("Bit %zu: %d (%s)\n", i, isCategorySet, categoryName);
	}
}

void InitCategories()
{
	gLoggingCategories.set(); // enable all categories by default
	MyLog("InitCategories call. %zu categories found.", gLoggingCategories.size());

#ifdef _DEBUG // enable everything in release builds to catch any issues
	// uncomment if you want a very quiet log
	// catlog_ClearBitsForMatchingCategories("Uncategorized");

	// disable noisy categories
	ClearBitsForMatchingCategories("LogSystem");
	ClearBitsForMatchingCategories("Environment");
	ClearBitsForMatchingCategories("Settings");
	ClearBitsForMatchingCategories("Licensing");
	ClearBitsForMatchingCategories("Editor");
	ClearBitsForMatchingCategories("Parser");
	//catlog_SetBitsForMatchingCategories("Parser.FileName"); // enable filenames
	ClearBitsForMatchingCategories("LowLevel");
#endif

	// disable categories based on registry, enabled both in debug and release builds
	DisableLoggingCategoriesFromRegistry();

	// listing all categories and whether they are enabled
	LogCategories("Listing log categories, and whether they are enabled");
	MyLog("\n");

#ifdef _DEBUG // this doesn't apply to end-users, so don't show it in release builds
	// help VA devs to find this function
	MyLog("To enable or disable log categories, go to the catlog_init() function or create `DisabledLoggingCategories` registry entry where `Logging` is located");
	MyLog("\n");
#endif
}
